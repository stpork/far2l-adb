#include <stdio.h>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <utils.h>
#include "Environment.h"

bool POpen(std::string &result, const char *command)
{
	Environment::ExplodeCommandLine cmd(command);
	if (cmd.empty()) {
		return false;
	}

	std::vector<char *> args;
	bool dev_null_stderr = false;
	for (auto &arg : cmd) {
		if (arg == "2>/dev/null") {
			dev_null_stderr = true;
		} else {
			args.push_back((char *)arg.c_str());
		}
	}
	args.push_back(nullptr);

	if (args.empty() || args[0] == nullptr) {
		return false;
	}

	int pipefd[2];
	if (pipe(pipefd) == -1) {
		perror("POpen: pipe");
		return false;
	}

	fflush(stdout);
	fflush(stderr);

	pid_t pid = fork();
	if (pid == -1) {
		perror("POpen: fork");
		close(pipefd[0]);
		close(pipefd[1]);
		return false;
	}

	if (pid == 0) {
		close(pipefd[0]);
		if (pipefd[1] != STDOUT_FILENO) {
			dup2(pipefd[1], STDOUT_FILENO);
			close(pipefd[1]);
		}
		if (dev_null_stderr) {
			int fd = open("/dev/null", O_WRONLY);
			if (fd != -1) {
				dup2(fd, STDERR_FILENO);
				close(fd);
			}
		}
		execvp(args[0], args.data());
		perror("POpen: execvp");
		_exit(127);
	}

	close(pipefd[1]);
	char buf[0x400];
	ssize_t count;
	while (true) {
		count = read(pipefd[0], buf, sizeof(buf));
		if (count > 0) {
			result.append(buf, count);
		} else if (count == 0) {
			break;
		} else if (errno != EINTR) {
			break;
		}
	}
	close(pipefd[0]);

	int status;
	waitpid(pid, &status, 0);
	return true;
}

bool POpen(std::vector<std::wstring> &result, const char *command)
{
	std::string tmp;
	bool out = POpen(tmp, command);

	for (size_t i = 0, ii = 0; i <= tmp.size(); ++i) {
		if (i == tmp.size() || tmp[i] == '\r' || tmp[i] == '\n') {
			if (i > ii) {
				result.emplace_back();
				StrMB2Wide(tmp.substr(ii, i - ii), result.back());
			}
			ii = i + 1;
		}
	}

	return out;
}
