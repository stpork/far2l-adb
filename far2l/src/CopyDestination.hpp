#pragma once

#include <atomic>
#include <cerrno>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "sudo.h"
#include "ScopeHelpers.h"
#include "CopyMetadata.hpp"
#include <fcntl.h>

// Keep an existing file in place until a complete replacement is available.
class CopyDestination
{
	std::string _directory;
	std::string _write_path;
	struct stat _original{};

public:
	bool create_new = false;
	~CopyDestination()
	{
		if (!_directory.empty()) {
			const int error = errno;
			sdc_unlink(_write_path.c_str());
			sdc_rmdir(_directory.c_str());
			errno = error;
		}
	}

	bool Prepare(const std::string &source, const std::string &destination, bool in_place)
	{
		struct stat dst{};
		if (sdc_lstat(destination.c_str(), &dst) != 0) {
			if (errno != ENOENT || in_place) return false;
			create_new = true;
			return true;
		}

		struct stat src{}, followed = dst;
		if (sdc_stat(source.c_str(), &src) != 0) return false;
		if ((!S_ISLNK(dst.st_mode) || sdc_stat(destination.c_str(), &followed) == 0)
				&& src.st_dev == followed.st_dev && src.st_ino == followed.st_ino) {
			errno = EINVAL;
			return false;
		}
		if (in_place || (!S_ISREG(dst.st_mode) && !S_ISLNK(dst.st_mode))) return true;

		_original = dst;
		const auto slash = destination.find_last_of('/');
		const auto parent = slash == std::string::npos ? "" : destination.substr(0, slash + 1);
		static std::atomic<unsigned> sequence{0};
		for (unsigned attempt = 0; attempt < 32; ++attempt) {
			auto directory = parent + ".far2l-copy-" + std::to_string(getpid()) + "-" + std::to_string(++sequence);
			auto path = directory + "/item";
			if (sdc_mkdir(directory.c_str(), 0700) == 0) {
				_directory.swap(directory);
				_write_path.swap(path);
				create_new = true;
				return true;
			}
			if (errno != EEXIST) return false;
		}
		return false;
	}

	const std::string &StagingPath() const { return _write_path; }
	bool Publish(const std::string &destination) const
	{
		if (_directory.empty()) return true;
		struct stat current{};
		if (sdc_lstat(destination.c_str(), &current) != 0) return false;
		if (current.st_dev != _original.st_dev || current.st_ino != _original.st_ino) {
			errno = EEXIST;
			return false;
		}
		return sdc_rename(_write_path.c_str(), destination.c_str()) == 0;
	}

	bool PublishFile(const std::string &destination, bool copy_mode, bool copy_xattrs) const
	{
		if (_directory.empty() || !S_ISREG(_original.st_mode)) return Publish(destination);
		FDScope original(sdc_open(destination.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK));
		struct stat current{}, staged{};
		if (!original.Valid() || fstat(original, &current) != 0) return false;
		if (current.st_dev != _original.st_dev || current.st_ino != _original.st_ino) {
			errno = EEXIST;
			return false;
		}
		if (sdc_lstat(_write_path.c_str(), &staged) != 0) return false;
		// A cloned file or a completed transfer may already have its final read-only mode.
		bool read_only = !(staged.st_mode & S_IWUSR);
		if (read_only && sdc_chmod(_write_path.c_str(), (staged.st_mode & 0777) | S_IWUSR) != 0)
			return false;
		FDScope file(sdc_open(_write_path.c_str(), O_WRONLY | O_NOFOLLOW));
		if (!file.Valid()) return false;
		if ((current.st_uid != staged.st_uid || current.st_gid != staged.st_gid)
				&& sdc_chown(_write_path.c_str(), current.st_uid, current.st_gid) != 0) return false;
		if ((!copy_mode || read_only) && sdc_fchmod(file, (copy_mode ? staged : current).st_mode & 0777) != 0)
			return false;
		if (!PreserveCopyAttributes(original, file, !copy_mode, !copy_xattrs)) return false;
		return Publish(destination);
	}
};
