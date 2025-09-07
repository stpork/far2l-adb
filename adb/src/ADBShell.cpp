// Local includes
#include "ADBShell.h"
#include "ADBLog.h"

// Standard library includes
#include <cstring>
#include <sstream>
#include <chrono>
#include <cstdarg>
#include <algorithm>

// System includes
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

static std::string _adbPath;    

// Get current time in microseconds
inline uint64_t nowMicros() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()
    ).count();
}

ADBShell::ADBShell(const std::string& device_serial)
    : _device_serial(device_serial)
    , _shell_pipe(nullptr)
    , _shell_stdin(-1)
    , _shell_pid(-1)
    , _is_running(false)
    , _session_id(getpid())  // Use process PID as session ID
    , _command_counter(0)
{
}

ADBShell::~ADBShell() {
    stop();
}

std::string ADBShell::findAdbExecutable() {
    const char* adb_paths[] = {
        "/opt/homebrew/bin/adb",
        "/usr/local/bin/adb", 
        "adb"
    };
    
    for (const char* path : adb_paths) {
        // Use direct popen for ADB version check (can't use adbCommand due to circular dependency)
        std::string test_command = std::string(path) + " version 2>&1";
        FILE *test_pipe = popen(test_command.c_str(), "r");
        if (test_pipe) {
            char buffer[256];
            if (fgets(buffer, sizeof(buffer), test_pipe)) {
                // Check if output contains "Android Debug Bridge" or version info
                std::string output(buffer);
                if (output.find("Android Debug Bridge") != std::string::npos || 
                    output.find("version") != std::string::npos) {
                    _adbPath = path;
                    pclose(test_pipe);
                    return path;  // Return the actual path
                }
            }
            pclose(test_pipe);
        }
    }
    return "";  // Return empty string if not found
}

std::string ADBShell::generateMarker() {
    uint64_t micros = nowMicros();
    uint32_t current_counter = _command_counter.fetch_add(1);
    
    // Create marker: string(nowMicros) + string(command_counter)
    return "__MARK_" + std::to_string(micros) + "_" + std::to_string(current_counter) + "__";
}

bool ADBShell::start() {
    if (_is_running) {
        return true; // Already running
    }

    if (_adbPath.empty()) {
        setError("ADB executable not found");
        return false;
    }

    int pipe_stdin[2] = {-1, -1};
    int pipe_stdout[2] = {-1, -1};

    if (pipe(pipe_stdin) < 0 || pipe(pipe_stdout) < 0) {
        setError("Failed to create pipes");
        if (pipe_stdin[0] >= 0) { 
            close(pipe_stdin[0]); 
            close(pipe_stdin[1]); 
        }
        if (pipe_stdout[0] >= 0) { 
            close(pipe_stdout[0]); 
            close(pipe_stdout[1]); 
        }
        return false;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // --- Child ---
        close(pipe_stdin[1]);
        close(pipe_stdout[0]);

        if (dup2(pipe_stdin[0], STDIN_FILENO) < 0 ||
            dup2(pipe_stdout[1], STDOUT_FILENO) < 0 ||
            dup2(pipe_stdout[1], STDERR_FILENO) < 0) {
            _exit(1);
        }

        // Close inherited FDs
        close(pipe_stdin[0]);
        close(pipe_stdout[1]);

        setenv("LANG", "en_US.UTF-8", 1);
        setenv("LC_ALL", "en_US.UTF-8", 1);
        setenv("TERM", "xterm", 1);

        if (_device_serial.empty()) {
            execlp(_adbPath.c_str(), "adb", "shell", (char*)nullptr);
        } else {
            execlp(_adbPath.c_str(), "adb", "-s", _device_serial.c_str(), "shell", (char*)nullptr);
        }

        // Only reached if execlp failed
        _exit(127);
    }
    else if (pid > 0) {
        // --- Parent ---
        close(pipe_stdin[0]);
        close(pipe_stdout[1]);

        _shell_pipe = fdopen(pipe_stdout[0], "r");
        _shell_stdin = pipe_stdin[1];
        _shell_pid = pid;

        if (!_shell_pipe) {
            setError("Failed to fdopen stdout pipe");
            close(pipe_stdout[0]);
            close(pipe_stdin[1]);
            return false;
        }

        // Check if adb died instantly
        int status;
        if (waitpid(pid, &status, WNOHANG) > 0) {
            setError("ADB shell terminated immediately");
            fclose(_shell_pipe);
            close(_shell_stdin);
            _shell_pipe = nullptr;
            _shell_stdin = -1;
            return false;
        }

        _is_running = true;
        _last_error.clear();
        return true;
    }
    else {
        setError("Failed to fork process");
        close(pipe_stdin[0]); close(pipe_stdin[1]);
        close(pipe_stdout[0]); close(pipe_stdout[1]);
        return false;
    }
}

bool ADBShell::writeCommand(const std::string& command, const std::string& marker) {
    if (!_is_running || _shell_stdin == -1) {
        setError("Shell not running");
        return false;
    }
    
    std::string full_command = command + "; echo " + marker + "\n";
    
    ssize_t written = write(_shell_stdin, full_command.c_str(), full_command.length());
    if (written != (ssize_t)full_command.length()) {
        setError("Failed to write command to shell");
        return false;
    }
    
    return true;
}

std::string ADBShell::readResponse(const std::string& marker) {
    if (!_is_running || !_shell_pipe) {
        setError("Shell not running");
        return "";
    }

    int fd = fileno(_shell_pipe);
    if (fd < 0) {
        setError("Invalid file descriptor");
        return "";
    }

    std::string response;
    char buffer[1024];
    bool marker_found = false;

    while (!marker_found) {
        ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            response += buffer;

            // Check for marker
            size_t pos = response.find(marker);
            if (pos != std::string::npos) {
                response = response.substr(0, pos);

                // Trim trailing newlines
                while (!response.empty() &&
                       (response.back() == '\n' || response.back() == '\r')) {
                    response.pop_back();
                }

                marker_found = true;
            }
        } else if (bytes_read == 0) {
            setError("Unexpected EOF from ADB shell");
            break;
        } else {
            if (errno == EINTR)
                continue; // retry on interrupt
            setError("Error reading from ADB shell: " + std::to_string(errno));
            break;
        }
    }
    return response;
}

std::string ADBShell::shellCommand(const std::string& command) {
    if (!_is_running) {
        if (!start()) {
            return "";
        }
    }
    std::string marker = generateMarker();
    if (!writeCommand(command, marker)) {
        return "";
    }
    std::string output = readResponse(marker);
    return output;
}

void ADBShell::stop() {
    if (_shell_pipe) {
        pclose(_shell_pipe);
        _shell_pipe = nullptr;
    }
    if (_shell_stdin != -1) {
        close(_shell_stdin);
        _shell_stdin = -1;
    }
    _is_running = false;
    _shell_pid = -1;
}

void ADBShell::setError(const std::string& error) {
    _last_error = error;
}

// Static version for global ADB commands (e.g., "adb devices -l")
std::string ADBShell::adbExec(const std::string& command) {
    // Ensure ADB path is available
    if (_adbPath.empty()) {
        _adbPath = findAdbExecutable();
        if (_adbPath.empty()) {
            return "";
        }
    }

    // Construct full command with stderr redirection
    std::string full_command = _adbPath + " " + command + " 2>&1";

    // Execute ADB command using popen
    FILE* pipe = popen(full_command.c_str(), "r");
    if (!pipe) {
        return "";
    }

    // Read output
    std::string output;
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }

    // Close pipe and check exit status
    int status = pclose(pipe);
    if (status == -1) {
        return "";
    }
    
    if (WIFEXITED(status)) {
        int exitCode = WEXITSTATUS(status);
        if (exitCode != 0) {
            return output; // Return output even on error for debugging
        }
    } else {
        return "";
    }

    // Trim trailing newline(s)
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }

    return output;
}

// Instance version for device-specific ADB commands with -s <device_serial>
std::string ADBShell::adbCommand(const std::string& command) const {
    // Construct device-specific command with -s <device_serial>
    std::string device_command;
    if (!_device_serial.empty()) {
        device_command = "-s " + _device_serial + " " + command;
    } else {
        device_command = command;
    }
    return ADBShell::adbExec(device_command);
}
