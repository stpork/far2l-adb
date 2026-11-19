#pragma once

#include <string>
#include <string_view>
#include <sys/types.h>

// Stage a complete local result on the same filesystem as its destination.
class StagedFile
{
	std::string _directory;
	std::string _path;
	std::string _destination;
	dev_t _previous_device{};
	ino_t _previous_inode{};
	bool _replace = false;
	bool _keep_backup = false;

public:
	explicit StagedFile(const std::string &destination, bool replace = false);
	~StagedFile();
	StagedFile(const StagedFile &) = delete;
	StagedFile &operator=(const StagedFile &) = delete;

	explicit operator bool() const { return !_path.empty(); }
	const std::string &Path() const { return _path; }
	const std::string &Directory() const { return _directory; }
	bool Publish();
};

bool IsLocalFileName(std::string_view name);
bool WriteFileAtomically(const std::string &path, std::string_view content, bool replace);
