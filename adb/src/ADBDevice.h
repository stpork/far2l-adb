#pragma once

// Standard library includes
#include <string>
#include <vector>
#include <memory>
#include <time.h>

// System includes
#include <sys/stat.h>

// FAR Manager includes
#include "farplug-wide.h"

// Forward declarations
class ADBShell;
struct PluginPanelItem;

// ADB Device implementation
class ADBDevice {
private:
    std::string _device_serial;
    std::string _current_path;
    std::unique_ptr<ADBShell> _adb_shell;
    bool _connected;
    
    void EnsureConnection();
    std::string ExtractPathFromPwd(const std::string &pwd_output);
    
    // Parse ls -la date/time format
    time_t ParseLsDateTime(const std::string &date, const std::string &time_str);

public:
    // Public methods for command execution
    std::string RunAdbCommand(const std::string &command);
    std::string RunShellCommand(const std::string &command);
    std::string GetCurrentWorkingDirectory();
    ADBDevice(const std::string &device_serial);
    virtual ~ADBDevice();
    
    // File operations
    std::string DirectoryEnum(const std::string &path, std::vector<PluginPanelItem> &files);
    bool SetDirectory(const std::string &path);
    
    // File transfer operations
    bool PullFile(const std::string &devicePath, const std::string &localPath);
    bool PushFile(const std::string &localPath, const std::string &devicePath);
    bool PullDirectory(const std::string &devicePath, const std::string &localPath);
    bool PushDirectory(const std::string &localPath, const std::string &devicePath);
    
    // File deletion operations
    bool DeleteFile(const std::string &devicePath);
    bool DeleteDirectory(const std::string &devicePath);
    
    // Directory creation operations
    bool CreateDirectory(const std::string &devicePath);

    // Connection management
    bool Connect();
    void Disconnect();
    bool IsConnected() const { return _connected; }
    
    // Utility functions
    std::string WStringToString(const std::wstring &wstr);
};
