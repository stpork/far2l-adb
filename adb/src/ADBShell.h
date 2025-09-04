#pragma once

// Standard library includes
#include <string>
#include <memory>
#include <cstdint>
#include <atomic>

void DebugLog(const char* format, ...);

class ADBShell {
public:
    ADBShell(const std::string& device_serial = "");
    ~ADBShell();
    
    // Disable copy constructor and assignment
    ADBShell(const ADBShell&) = delete;
    ADBShell& operator=(const ADBShell&) = delete;
    
    // Start the ADB shell process
    bool start();
    // Execute a command and return the output
    std::string shellCommand(const std::string& command);
    // Execute a device-specific ADB command with -s <device_serial> - instance version
    std::string adbCommand(const std::string& command) const;
    // Execute a global ADB command (e.g., "adb devices -l") - static version
    static std::string adbExec(const std::string& command);
    // Stop the shell process
    void stop();

private:
    std::string _device_serial;
    FILE* _shell_pipe;
    int _shell_stdin;  // File descriptor for writing to shell stdin
    int _shell_pid;
    bool _is_running;
    std::string _last_error;
    
    // Session management
    uint32_t _session_id;
    std::atomic<uint32_t> _command_counter;
    
    // Private methods
    static std::string findAdbExecutable();
    std::string generateMarker();
    bool writeCommand(const std::string& command, const std::string& marker);
    std::string readResponse(const std::string& marker);
    void setError(const std::string& error);
};
