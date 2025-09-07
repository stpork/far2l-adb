#include "ADBDevice.h"
#include "ADBShell.h"
#include "ADBLog.h"
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <wchar.h>
#include <errno.h>
#include <utils.h>

ADBDevice::ADBDevice(const std::string &device_serial)
    : _device_serial(device_serial), _current_path("/"), _adb_shell(nullptr), _connected(false)
{
    Connect();
}

ADBDevice::~ADBDevice()
{
    Disconnect();
}

bool ADBDevice::Connect()
{
    if (_connected) {
        return true;
    }
    
    try {
        _adb_shell = std::make_unique<ADBShell>(_device_serial);
        if (!_adb_shell->start()) {
            return false;
        }
        std::string pwd_response = _adb_shell->shellCommand("pwd");
        if (pwd_response.empty()) {
            return false;
        }
        _current_path = ExtractPathFromPwd(pwd_response);
        _connected = true;
        return true;
        
    } catch (const std::exception& e) {
        return false;
    }
}

void ADBDevice::Disconnect()
{
    if (_adb_shell) {
        _adb_shell->stop();
        _adb_shell.reset();
    }
    _connected = false;
}

void ADBDevice::EnsureConnection()
{
    if (!_connected || !_adb_shell) {
        if (!Connect()) {
            throw std::runtime_error("Failed to connect to ADB shell");
        }
    }
}

std::string ADBDevice::RunAdbCommand(const std::string &command) {
    if (!_adb_shell) {
        return "";
    }

    std::string full_command;
    if (!_device_serial.empty()) {
        full_command = "-s " + _device_serial + " " + command;
    } else {
        full_command = command;
    }
    
    return ADBShell::adbExec(full_command);
}

std::string ADBDevice::RunShellCommand(const std::string &command)
{
    EnsureConnection();
    
    if (!_connected || !_adb_shell) {
        return "";
    }
    
    try {
        std::string output = _adb_shell->shellCommand(command);
        return output;
        
    } catch (const std::exception& e) {
        return "";
    }
}

std::string ADBDevice::GetCurrentWorkingDirectory()
{
    if (!_connected || !_adb_shell) {
        return "/";
    }
    
    try {
        std::string pwd_output = _adb_shell->shellCommand("pwd");
        return ExtractPathFromPwd(pwd_output);
    } catch (const std::exception& e) {
        return "/";
    }
}

std::string ADBDevice::DirectoryEnum(const std::string &path, std::vector<PluginPanelItem> &files)
{

    if (!_connected || !_adb_shell) {
        throw std::runtime_error("ADB shell not connected");
    }

    const std::string separator = "<<<!>>>";
    const std::string arrow = ":->";

    // Bulk command: cd, pwd, ls -la, then symlink info (properly quote path for spaces)
    std::ostringstream bulk_cmd;
    bulk_cmd << "cd \"" << path << "\" 2>/dev/null; pwd; ls -la; echo \"" << separator << "\";"
             << "for f in *; do "
             << "[ -L \"$f\" ] && ([ -d \"$f\" ] && echo \"$f" << arrow << "D\" "
             << "|| ([ -f \"$f\" ] && echo \"$f" << arrow << "F\" || echo \"$f" << arrow << "B\")); "
             << "done";
    
    std::string bulk_output = RunShellCommand(bulk_cmd.str());

    std::vector<std::string> ls_lines;
    std::vector<std::string> symlink_info;
    std::string current_path;
    bool after_separator = false;

    // Split lines once
    std::istringstream output_stream(bulk_output);
    std::string line;
    while (std::getline(output_stream, line)) {
        if (line.empty()) continue;
        if (line == separator) { after_separator = true; continue; }

        if (!after_separator) {
            if (current_path.empty()) {
                current_path = ExtractPathFromPwd(line);
                _current_path = current_path;
            } else {
                ls_lines.push_back(line);
            }
        } else {
            symlink_info.push_back(line);
        }
    }

    // Add hardcoded ".." entry
    files.clear();

    // Parse ls -la lines
    for (const auto& ls_line : ls_lines) {
        if (ls_line.find("Permission denied") != std::string::npos || ls_line.find("total") == 0)
            continue;
        if (ls_line.find('?') != std::string::npos)
            continue;

        std::istringstream ls_stream(ls_line);
        std::string perms, links, owner, group, size, date, time_str;
        if (!(ls_stream >> perms >> links >> owner >> group >> size >> date >> time_str))
            continue;

        std::string rest;
        std::getline(ls_stream, rest);
        if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);

        std::string filename = rest;
        std::string symlink_target;
        bool is_symlink = (perms[0] == 'l');
        if (is_symlink) {
            auto pos = rest.find(" -> ");
            if (pos != std::string::npos) {
                filename = rest.substr(0, pos);
                symlink_target = rest.substr(pos + 4);
            }
        }

        if (filename.empty() || filename == "." || filename == "..") continue;

        PluginPanelItem item{};
        item.FindData.lpwszFileName = wcsdup(StrMB2Wide(filename).c_str());
        item.FindData.dwUnixMode = (perms[0] == 'd') ? (S_IFDIR | 0755) : (is_symlink ? (S_IFLNK | 0644) : (S_IFREG | 0644));
        item.FindData.dwFileAttributes = WINPORT(EvaluateAttributesA)(item.FindData.dwUnixMode, filename.c_str());
        if (perms[0] == 'd') item.FindData.dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;

        if (is_symlink)
            item.Description = wcsdup(symlink_target.empty() ? L"Symlink (no target)" : StrMB2Wide(symlink_target).c_str());

        try { item.FindData.nFileSize = item.FindData.nPhysicalSize = std::stoull(size); } catch (...) { item.FindData.nFileSize = item.FindData.nPhysicalSize = 0; }

        item.Owner = wcsdup(StrMB2Wide(owner).c_str());
        item.Group = wcsdup(StrMB2Wide(group).c_str());
        try { item.NumberOfLinks = std::stoi(links); } catch (...) { item.NumberOfLinks = 1; }

        FILETIME ft{};
        time_t t = ParseLsDateTime(date, time_str);
        if (!t) t = time(nullptr);
        ULARGE_INTEGER uli; uli.QuadPart = (t * 10000000ULL) + 116444736000000000ULL;
        ft.dwLowDateTime = uli.LowPart; ft.dwHighDateTime = uli.HighPart;
        item.FindData.ftCreationTime = item.FindData.ftLastAccessTime = item.FindData.ftLastWriteTime = ft;

        files.push_back(item);
    }

    // Map filenames for fast symlink update
    std::unordered_map<std::string, PluginPanelItem*> file_map;
    for (auto& f : files)
        		if (f.FindData.lpwszFileName)
			file_map[StrWide2MB(f.FindData.lpwszFileName)] = &f;

    for (const auto& symlink_line : symlink_info) {
        auto colon_pos = symlink_line.find(arrow);
        if (colon_pos == std::string::npos) continue;
        std::string filename = symlink_line.substr(0, colon_pos);
        std::string type = symlink_line.substr(colon_pos + arrow.size());

        auto it = file_map.find(filename);
        if (it != file_map.end()) {
            PluginPanelItem* file = it->second;
            if (type == "D") file->FindData.dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
            // F and B need no action
        }
    }

    return current_path.empty() ? path : current_path;
}

std::string ADBDevice::ExtractPathFromPwd(const std::string &pwd_output)
{
    std::string path = pwd_output;
    
    // Remove trailing newline
    if (!path.empty() && path[path.length()-1] == '\n') {
        path.erase(path.length()-1);
    }
    
    // Remove trailing carriage return if present
    if (!path.empty() && path[path.length()-1] == '\r') {
        path.erase(path.length()-1);
    }
    
    return path;
}

time_t ADBDevice::ParseLsDateTime(const std::string &date, const std::string &time_str) {
    struct tm timeinfo = {};
    time_t result = 0;
    int hour = 0, minute = 0;

    // Parse time if present
    sscanf(time_str.c_str(), "%d:%d", &hour, &minute);
    timeinfo.tm_hour = hour;
    timeinfo.tm_min = minute;
    timeinfo.tm_sec = 0;

    if (date.find('-') != std::string::npos) {
        int year, month, day;
        if (sscanf(date.c_str(), "%d-%d-%d", &year, &month, &day) == 3) {
            timeinfo.tm_year = year - 1900;
            timeinfo.tm_mon = month - 1;
            timeinfo.tm_mday = day;
            result = mktime(&timeinfo);
        }
    } else {
        // "MMM DD" format
        const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        int month = -1, day;
        for (int i=0;i<12;i++) if (date.find(months[i]) != std::string::npos) { month=i; break; }
        if (month != -1 && sscanf(date.c_str(), "%*s %d", &day) == 1) {
            time_t now = time(nullptr);
            struct tm* current_time = localtime(&now);
            timeinfo.tm_year = current_time->tm_year; // current year
            timeinfo.tm_mon = month;
            timeinfo.tm_mday = day;
            result = mktime(&timeinfo);
        }
    }

    if (result == 0) result = time(nullptr); // fallback
    return result;
}

bool ADBDevice::SetDirectory(const std::string &path) {
    
    if (!_connected || !_adb_shell) {
        return false;
    }
    
    // Execute cd command and get new working directory (properly quote path for spaces)
    std::string cd_command = "cd \"" + path + "\" 2>/dev/null && pwd";
    std::string result = RunShellCommand(cd_command);
    
    if (result.empty()) {
        return false;
    }
    
    // Extract the new path from pwd output
    std::string new_path = ExtractPathFromPwd(result);
    if (new_path.empty()) {
        return false;
    }
    
    // Update current path
    _current_path = new_path;
    return true;
}

int ADBDevice::PullFile(const std::string &devicePath, const std::string &localPath) {
    DBG("devicePath='%s', localPath='%s'\n", devicePath.c_str(), localPath.c_str());
    EnsureConnection();
    if (!_connected) {
        DBG("Not connected, returning EIO\n");
        return EIO; // Input/output error for device not connected
    }
    
    std::string command = "pull \"" + devicePath + "\" \"" + localPath + "\"";
    DBG("command='%s'\n", command.c_str());
    std::string result = RunAdbCommand(command);
    DBG("result='%s'\n", result.c_str());
    
    if (result.find("file pulled") != std::string::npos || 
        result.find("skipped") != std::string::npos ||
        result.empty()) {
        DBG("Success\n");
        return 0;
    } else {
        int err = Str2Errno(result);
        DBG("Error %d\n", err);
        return err;
    }
}

int ADBDevice::PushFile(const std::string &localPath, const std::string &devicePath) {
    EnsureConnection();
    if (!_connected) {
        return EIO; // Input/output error for device not connected
    }
    
    std::string command = "push \"" + localPath + "\" \"" + devicePath + "\"";
    std::string result = RunAdbCommand(command);
    
    if (result.empty()) {
        return 0;
    } else {
        return Str2Errno(result);
    }
}

int ADBDevice::PullDirectory(const std::string &devicePath, const std::string &localPath) {
    EnsureConnection();
    if (!_connected) {
        return EIO; // Input/output error for device not connected
    }
    
    std::string command = "pull \"" + devicePath + "\" \"" + localPath + "\"";
    std::string result = RunAdbCommand(command);
    
    if (result.find("file pulled") != std::string::npos || 
        result.find("skipped") != std::string::npos ||
        result.find("files pulled") != std::string::npos ||
        result.empty()) {
        return 0;
    } else {
        return Str2Errno(result);
    }
}

int ADBDevice::PushDirectory(const std::string &localPath, const std::string &devicePath) {
    EnsureConnection();
    if (!_connected) {
        return EIO; // Input/output error for device not connected
    }
    
    std::string command = "push \"" + localPath + "\" \"" + devicePath + "\"";
    std::string result = RunAdbCommand(command);
    
    if (result.find("file pushed") != std::string::npos || 
        result.find("skipped") != std::string::npos ||
        result.find("files pushed") != std::string::npos ||
        result.empty()) {
        return 0;
    } else {
        return Str2Errno(result);
    }
}


int ADBDevice::DeleteFile(const std::string &devicePath) {
    EnsureConnection();
    if (!_connected) {
        return EIO; // Input/output error for device not connected
    }
    
    std::string command = "rm \"" + devicePath + "\"";
    std::string result = RunShellCommand(command);
    
    if (result.empty()) {
        return 0;
    } else {
        return Str2Errno(result);
    }
}

int ADBDevice::DeleteDirectory(const std::string &devicePath) {
    EnsureConnection();
    if (!_connected) {
        return EIO; // Input/output error for device not connected
    }
    
    std::string command = "rm -rf \"" + devicePath + "\"";
    std::string result = RunShellCommand(command);
    
    if (result.empty()) {
        return 0;
    } else {
        return Str2Errno(result);
    }
}

int ADBDevice::CreateDirectory(const std::string &devicePath) {
    EnsureConnection();
    if (!_connected) {
        return EIO; // Input/output error for device not connected
    }
    
    std::string command = "mkdir -p \"" + devicePath + "\"";
    std::string result = RunShellCommand(command);
    
    if (result.empty()) {
        return 0;
    } else {
        return Str2Errno(result);
    }
}

int ADBDevice::Str2Errno(const std::string &adbError) {
    static const std::vector<std::pair<const char*, int>> errorMap = {
        {"remote object", ENOENT},
        {"does not exist", ENOENT},
        {"No such file or directory", ENOENT},
        {"File exists", EEXIST},
        {"Permission denied", EACCES},
        {"insufficient permissions for device", EACCES},
        {"No space left on device", ENOSPC},
        {"Read-only file system", EROFS},
        {"Broken pipe", EPIPE},
        {"error: closed", EPIPE},
        {"Operation not permitted", EPERM},
        {"Directory not empty", ENOTEMPTY},
        {"Device not found", ENODEV},
        {"no devices/emulators found", ENODEV},
        {"more than one device/emulator", EINVAL}
    };

    for (const auto& [key, code] : errorMap) {
        if (adbError.find(key) != std::string::npos) {
            return code;
        }
    }

    return EIO;
}