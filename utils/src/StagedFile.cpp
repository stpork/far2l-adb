#include "StagedFile.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif

static int RenameWithFlags(const char *from, const char *to, bool swap)
{
#ifdef __APPLE__
	return renamex_np(from, to, swap ? RENAME_SWAP : RENAME_EXCL);
#elif defined(__linux__) && defined(SYS_renameat2)
	return syscall(SYS_renameat2, AT_FDCWD, from, AT_FDCWD, to, swap ? 2 : 1);
#else
	errno = ENOTSUP;
	return -1;
#endif
}

bool IsLocalFileName(std::string_view name)
{
	return !name.empty() && name != "." && name != ".."
		&& name.find('/') == name.npos && name.find('\0') == name.npos;
}

StagedFile::StagedFile(const std::string &destination, bool replace)
	: _destination(destination)
{
	if (replace) {
		struct stat previous{};
		if (lstat(destination.c_str(), &previous) == 0) {
			_replace = true;
			_previous_device = previous.st_dev;
			_previous_inode = previous.st_ino;
		} else if (errno != ENOENT) {
			return;
		}
	}
	const auto slash = destination.find_last_of('/');
	_directory = (slash == std::string::npos ? "" : destination.substr(0, slash + 1))
		+ ".far2l-stage-XXXXXX";
	_path = _directory + "/item";
	if (mkdtemp(_directory.data())) {
		_path.replace(0, _directory.size(), _directory);
	} else {
		_directory.clear();
		_path.clear();
	}
}

StagedFile::~StagedFile()
{
	if (!_directory.empty() && !_keep_backup) {
		const int error = errno;
		std::error_code ec;
		try { std::filesystem::remove_all(_directory, ec); }
		catch (const std::bad_alloc &) {}
		errno = error;
	}
}

static bool UnsupportedRenameFlags()
{
	return errno == ENOSYS || errno == ENOTSUP || errno == EINVAL;
}

bool StagedFile::Publish()
{
	if (_path.empty()) {
		errno = EINVAL;
		return false;
	}
	const char* destination = _destination.c_str();
	if (!_replace) {
		if (RenameWithFlags(_path.c_str(), destination, false) == 0) return true;
		if (!UnsupportedRenameFlags()) return false;
		struct stat staged{};
		if (lstat(_path.c_str(), &staged) != 0) return false;
		// A link does not clobber a concurrent destination. For directories, reserve
		// an empty destination first, then replace our own reservation.
		if (!S_ISDIR(staged.st_mode)) return link(_path.c_str(), destination) == 0;
		if (mkdir(destination, 0700) != 0) return false;
		if (rename(_path.c_str(), destination) == 0) return true;
		const int error = errno;
		rmdir(destination);
		errno = error;
		return false;
	}

	struct stat previous{};
	if (lstat(destination, &previous) != 0) return false;
	if (previous.st_dev != _previous_device || previous.st_ino != _previous_inode) {
		errno = EEXIST;
		return false;
	}
	if (rename(_path.c_str(), destination) == 0) return true;
	if (errno != ENOTEMPTY && errno != EEXIST && errno != EISDIR && errno != ENOTDIR) return false;

	// Nonempty directories and changes of object type require a swap or backup.
	if (RenameWithFlags(_path.c_str(), destination, true) == 0) return true;
	if (!UnsupportedRenameFlags()) return false;
	const std::string backup = _directory + "/previous";
	if (rename(destination, backup.c_str()) != 0) return false;
	_replace = false;
	if (Publish()) return true;
	const int error = errno;
	// Never overwrite a new destination during rollback. Leave our backup intact.
	if (RenameWithFlags(backup.c_str(), destination, false) != 0) {
		struct stat current{};
		if (!UnsupportedRenameFlags() || lstat(destination, &current) == 0
				|| errno != ENOENT || rename(backup.c_str(), destination) != 0) {
			_keep_backup = true;
			fprintf(stderr, "far2l: previous destination retained at '%s'\n", backup.c_str());
		}
	}
	errno = error;
	return false;
}

bool WriteFileAtomically(const std::string &path, std::string_view content, bool replace)
{
	StagedFile stage(path, replace);
	if (stage.Path().empty()) return false;
	const int fd = open(stage.Path().c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (fd < 0) return false;
	int error = 0;
	while (!content.empty()) {
		const auto n = write(fd, content.data(), content.size());
		if (n > 0) content.remove_prefix(n);
		else if (n == 0 || errno != EINTR) {
			error = n == 0 ? EIO : errno;
			break;
		}
	}
	if (!error && fsync(fd) != 0) error = errno;
	if (close(fd) != 0 && !error) error = errno;
	if (!error) return stage.Publish();
	errno = error;
	return false;
}
