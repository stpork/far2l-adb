#pragma once

// Standard library includes
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <time.h>
#include <functional>

// System includes
#include <sys/stat.h>

// FAR Manager includes
#include "farplug-wide.h"

// Forward declarations
class ADBShell;
struct PluginPanelItem;

// Per-file progress callback. percent: 0-100; path: adb's reported
// path for the current file (device-side on push, host-side on pull;
// empty on synthetic 0%/100% boundaries from start()/complete()).
using AdbProgressFn = std::function<void(int, const std::string&)>;

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

    // Helper to build args with device serial prefix
    std::vector<std::string> BuildArgs(const std::vector<std::string>& args) const;

    // Helper to check if result indicates success
    bool IsSuccessResult(const std::string& result, bool is_push = false) const;

    // Unified transfer helper (DRY)
    int TransferItem(const std::string& src, const std::string& dst, bool is_push, bool recursive,
                    const AdbProgressFn& on_progress = {},
                    const std::function<bool()>& abort_check = {});

public:
    // Public methods for command execution
    std::string RunAdbCommand(const std::string &command);
    std::string RunAdbCommand(const std::vector<std::string> &args);
    std::string RunAdbCommand(const std::vector<std::string> &args, const std::function<void(const std::string&)> &on_chunk);
    std::string RunAdbCommandWithProgress(const std::vector<std::string> &args, const std::function<void(const std::string&)> &on_chunk, const std::function<bool()> &abort_check = {});
    std::string RunShellCommand(const std::string &command);
    // Exit code of the most recent RunShellCommand(); -1 if unavailable.
    int LastShellExitCode() const;
    // Run shell command and stream output via callback (bypasses persistent session echo issues)
    void RunShellCommandStreaming(const std::string &command, const std::function<void(const std::string&)> &on_line);
    std::string GetCurrentWorkingDirectory();
    ADBDevice(const std::string &device_serial);
    virtual ~ADBDevice();

    // File operations
    std::string DirectoryEnum(const std::string &path, std::vector<PluginPanelItem> &files);
    bool SetDirectory(const std::string &path);

    // File transfer operations
    int PullFile(const std::string &devicePath, const std::string &localPath);
    int PullFile(const std::string &devicePath, const std::string &localPath, const AdbProgressFn &on_progress, const std::function<bool()> &abort_check = {});
    int PushFile(const std::string &localPath, const std::string &devicePath);
    int PushFile(const std::string &localPath, const std::string &devicePath, const AdbProgressFn &on_progress, const std::function<bool()> &abort_check = {});
    int PullDirectory(const std::string &devicePath, const std::string &localPath);
    int PullDirectory(const std::string &devicePath, const std::string &localPath, const AdbProgressFn &on_progress, const std::function<bool()> &abort_check = {});
    int PushDirectory(const std::string &localPath, const std::string &devicePath);
    int PushDirectory(const std::string &localPath, const std::string &devicePath, const AdbProgressFn &on_progress, const std::function<bool()> &abort_check = {});

    // File deletion operations
    int DeleteFile(const std::string &devicePath);
    int DeleteDirectory(const std::string &devicePath);

    // Directory creation operations
    int CreateDirectory(const std::string &devicePath);
    int CopyRemote(const std::string &srcDevicePath, const std::string &dstDeviceDir);
    int MoveRemote(const std::string &srcDevicePath, const std::string &dstDeviceDir);
    // *As variants take a full destination PATH (caller renames in
    // flight) instead of a destination directory; used by Shift+F5/F6.
    int CopyRemoteAs(const std::string &srcDevicePath, const std::string &dstDevicePath);
    int MoveRemoteAs(const std::string &srcDevicePath, const std::string &dstDevicePath);

    // File existence check
    bool FileExists(const std::string &devicePath);
    bool IsDirectory(const std::string &devicePath);

    // Directory info (file count, total size in bytes)
    struct DirectoryInfo {
        uint64_t file_count;
        uint64_t total_size;
    };
    DirectoryInfo GetDirectoryInfo(const std::string &devicePath);

    // Per-file basename → size, walked under devicePath. Single shell
    // call. Used by the progress dialog so adb pull's per-file bar can
    // resize as adb advances from one file to the next.
    void GetDirectoryFileSizes(const std::string &devicePath,
                                std::unordered_map<std::string, uint64_t>& out);

    // Connection management
    bool Connect();
    void Disconnect();
    bool IsConnected() const { return _connected; }
    std::string GetDeviceSerial() const { return _device_serial; }
    std::string GetCurrentPath() const { return _current_path; }
    void SyncPath();  // Query shell's actual cwd and update internal path

    // Utility functions
    std::string WStringToString(const std::wstring &wstr);

    // Error mapping
    static int Str2Errno(const std::string &adbError);

    // Static helper for PluginPanelItem memory management
    static wchar_t* AllocateItemString(const std::string& s);
};

// Path utility functions (shared across ADB classes)
namespace ADBUtils {
    // Join path components, ensuring proper separator
    std::string JoinPath(const std::string& base, const std::string& component);

    // Trims trailing newlines/carriage returns
    void TrimTrailingNewlines(std::string& s);

    // Shell quoting for paths with spaces
    std::string ShellQuote(const std::string& s);

    // Check if connection is available, returns EIO if not
    int CheckConnection(bool connected);
}

// Parses adb push/pull -p output streams. Each line is "[ NN%] /path".
// Per-line callback fires with both percent and the file path adb is
// currently transferring; start()/complete() fire with empty path.
class ProgressParser {
public:
    ProgressParser(AdbProgressFn on_progress, bool debug_log = false);

    void operator()(const std::string &chunk);
    void drain();
    void complete();
    void start();

private:
    AdbProgressFn _on_progress;
    bool _debug_log;
    int _last_percent;
    std::string _last_path;
    std::string _pending;

    // Returns true if the line had a "[ NN%] ..." form; out params filled.
    static bool ExtractProgress(const std::string &s, int &percent, std::string &path);
};
