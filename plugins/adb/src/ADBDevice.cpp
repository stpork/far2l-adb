#include <StagedFile.h>
#include <string_view>
#include <charconv>
#include <sys/stat.h>
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
#include <fstream>
#include <chrono>
#include <cctype>

// --- ADBUtils ---
namespace ADBUtils {

std::string ShellQuote(const std::string &value)
{
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') {
            quoted += "'\"'\"'";
        } else {
            quoted.push_back(c);
        }
    }
    quoted += "'";
    return quoted;
}

std::string JoinPath(const std::string& base, const std::string& component)
{
    if (base.empty()) return component;
    if (component.empty()) return base;

    bool baseEndsSep = (!base.empty() && (base.back() == '/'));
    bool compStartsSep = (!component.empty() && (component.front() == '/'));

    if (baseEndsSep && compStartsSep) {
        return base + component.substr(1);
    } else if (!baseEndsSep && !compStartsSep) {
        return base + "/" + component;
    }
    return base + component;
}

void TrimTrailingNewlines(std::string& s)
{
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
        s.pop_back();
    }
}

// "drwxr-xr-x" -> S_IFDIR|0755. Panels need the real bits: far2l derives the permission
// column, the executable/symlink highlighting and the attribute flags from dwUnixMode, so a
// hardcoded 0755/0644 makes all three wrong.
// NB: a fixed variant of NetRocks' ShellParseUtils::{Char2FileType,Triplet2FileMode,Str2Mode},
// which is private to that plugin, omits the setuid/setgid/sticky bits and tests the wrong
// character for 's'. Promoting a shared implementation to utils/ would suit both.
mode_t LsPermsToMode(const std::string &perms)
{
    static const auto triplet = [](const char *c) -> mode_t {
        mode_t out = 0;
        if (c[0] == 'r') out |= 4;
        if (c[1] == 'w') out |= 2;
        // x, or s/t which mean "set-id/sticky *and* executable"; S/T mean the bit without x.
        if (c[2] == 'x' || c[2] == 's' || c[2] == 't') out |= 1;
        return out;
    };

    mode_t mode = 0;
    switch (perms.empty() ? '-' : perms[0]) {
        case 'd': mode = S_IFDIR; break;
        case 'l': mode = S_IFLNK; break;
        case 'c': mode = S_IFCHR; break;
        case 'b': mode = S_IFBLK; break;
        case 'p': mode = S_IFIFO; break;
        case 's': mode = S_IFSOCK; break;
        default:  mode = S_IFREG; break;
    }
    // Entries the shell could not stat come back as "d?????????" - keep the type, no bits.
    if (perms.size() >= 4)  mode |= triplet(perms.c_str() + 1) << 6;
    if (perms.size() >= 7)  mode |= triplet(perms.c_str() + 4) << 3;
    if (perms.size() >= 10) mode |= triplet(perms.c_str() + 7);

    // The set-id and sticky bits live in the x positions as s/S (user, group) and t/T (other);
    // far2l renders them in the permission column, so losing them shows a setuid binary as a
    // plain -rwxr-xr-x.
    if (perms.size() >= 4  && (perms[3] == 's' || perms[3] == 'S')) mode |= S_ISUID;
    if (perms.size() >= 7  && (perms[6] == 's' || perms[6] == 'S')) mode |= S_ISGID;
    if (perms.size() >= 10 && (perms[9] == 't' || perms[9] == 'T')) mode |= S_ISVTX;
    return mode;
}

int CheckConnection(bool connected)
{
    return connected ? 0 : EIO;
}

std::string PathBasename(const std::string& p)
{
    if (p.empty()) return std::string();
    size_t end = p.size();
    while (end > 0 && p[end - 1] == '/') --end;
    if (end == 0) return std::string();
    size_t start = end;
    while (start > 0 && p[start - 1] != '/') --start;
    return p.substr(start, end - start);
}

} // namespace ADBUtils


// --- ProgressParser ---
ProgressParser::ProgressParser(AdbProgressFn on_progress, bool debug_log)
    : _on_progress(std::move(on_progress)), _debug_log(debug_log), _last_percent(-1) {}

// ECMA-48 state machine — per-byte, state survives chunks; drops controls, keeps printable + \r\n\t.
void ProgressParser::VtFeed(const std::string &chunk) {
    for (char c : chunk) {
        unsigned char b = static_cast<unsigned char>(c);
        switch (_vt_state) {
            case VT_NORMAL:
                // C1 controls (0x80-0x9F) NOT decoded — adb output is UTF-8 where those are continuation bytes.
                if (b == 0x1B) { _vt_state = VT_ESC; }
                else if (b == '\r' || b == '\n' || b == '\t') { _pending.push_back(c); }
                else if (b >= 0x20 && b != 0x7F) { _pending.push_back(c); }
                break;
            case VT_ESC:
                if (b == '[')      _vt_state = VT_CSI;
                else if (b == ']') _vt_state = VT_OSC;
                else if (b == ' ' || b == '#' || b == '(' || b == ')'
                      || b == '*' || b == '+' || b == '%')
                    _vt_state = VT_ESC_INTERM;  // 2-byte intro: ESC <int> <final>
                else _vt_state = VT_NORMAL;     // single-char ESC <X>: discard X
                break;
            case VT_ESC_INTERM:
                _vt_state = VT_NORMAL;
                break;
            case VT_CSI:
                // Params 0x30-0x3F, intermediates 0x20-0x2F, terminator 0x40-0x7E.
                if (b >= 0x40 && b <= 0x7E) _vt_state = VT_NORMAL;
                // else: param/intermediate — discard, stay in CSI
                break;
            case VT_OSC:
                if (b == 0x07) _vt_state = VT_NORMAL;       // BEL terminator
                else if (b == 0x1B) _vt_state = VT_OSC_ESC; // possible ST ahead
                // else: OSC payload — discard
                break;
            case VT_OSC_ESC:
                if (b == '\\') _vt_state = VT_NORMAL;  // ST (string terminator)
                else _vt_state = VT_ESC;               // restart escape parse
                break;
        }
    }
}

void ProgressParser::operator()(const std::string &chunk) {
    if (_debug_log) {
        DBG(" PTY chunk received (%zu bytes): [", chunk.size());
        for (unsigned char c : chunk) {
            if (c >= 32 && c < 127) DBG("%c", c);
            else                    DBG("\\x%02X", c);
        }
        DBG("]\n");
    }

    VtFeed(chunk);
    size_t split = 0;
    while ((split = _pending.find_first_of("\r\n")) != std::string::npos) {
        std::string line = _pending.substr(0, split);
        _pending.erase(0, split + 1);
        int percent = -1;
        std::string path;
        if (!ExtractProgress(line, percent, path)) continue;
        // Fire on percent OR path change so file boundaries surface.
        if (percent != _last_percent || path != _last_path) {
            _last_percent = percent;
            _last_path = path;
            _on_progress(percent, path);
        }
    }
}

void ProgressParser::drain() {
    if (!_pending.empty()) {
        int percent = -1;
        std::string path;
        if (ExtractProgress(_pending, percent, path)
                && (percent != _last_percent || path != _last_path)) {
            _on_progress(percent, path);
        }
        _pending.clear();
    }
}

void ProgressParser::complete() { _on_progress(100, std::string()); }
void ProgressParser::start()    { _on_progress(0,   std::string()); }

bool ProgressParser::ExtractProgress(const std::string &s, int &percent, std::string &path) {
    // Input is VT-cleaned. Find the LAST '%' so an in-path '%' can't poison detection.
    size_t p = s.rfind('%');
    if (p == std::string::npos || p == 0) return false;
    size_t start = p;
    while (start > 0 && std::isdigit(static_cast<unsigned char>(s[start - 1]))) --start;
    if (start == p) return false;
    int v = std::atoi(s.substr(start, p - start).c_str());
    if (v < 0 || v > 100) return false;
    percent = v;
    path.clear();

    // Legacy adb -p: "[<spaces>NN%] /path" — variable padding ("[  5%]" / "[100%]").
    bool legacy = false;
    {
        size_t i = start;
        while (i > 0 && s[i - 1] == ' ') --i;
        if (i > 0 && s[i - 1] == '[') legacy = true;
    }
    if (legacy) {
        size_t close = s.find(']', p);
        if (close != std::string::npos) {
            size_t ps = close + 1;
            while (ps < s.size() && std::isspace(static_cast<unsigned char>(s[ps]))) ++ps;
            path.assign(s, ps, std::string::npos);
        }
    }
    // Modern adb -p format: "<path>: NN%[\x1B[K]" — ": " just before digits.
    else if (start >= 2 && s[start - 1] == ' ' && s[start - 2] == ':') {
        path.assign(s, 0, start - 2);
    }
    // Strip leading "[NN%]" batch prefix from adb's combined format — varying prefix would otherwise spam path-changes and inflate file count.
    if (!path.empty() && path[0] == '[') {
        size_t close = path.find(']');
        if (close != std::string::npos) {
            bool only_pct_inside = true;
            for (size_t i = 1; i < close; ++i) {
                char c = path[i];
                if (!(c == ' ' || std::isdigit(static_cast<unsigned char>(c)) || c == '%')) {
                    only_pct_inside = false; break;
                }
            }
            if (only_pct_inside) {
                size_t ps = close + 1;
                while (ps < path.size() && std::isspace(static_cast<unsigned char>(path[ps]))) ++ps;
                path.erase(0, ps);
            }
        }
    }
    // Path leftover: trailing whitespace from the source line.
    while (!path.empty()) {
        unsigned char c = static_cast<unsigned char>(path.back());
        if (std::isspace(c)) { path.pop_back(); continue; }
        break;
    }
    return true;
}

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

std::vector<std::string> ADBDevice::BuildArgs(const std::vector<std::string>& args) const
{
    std::vector<std::string> full_args;
    if (!_device_serial.empty()) {
        full_args.emplace_back("-s");
        full_args.emplace_back(_device_serial);
    }
    full_args.insert(full_args.end(), args.begin(), args.end());
    return full_args;
}

std::string ADBDevice::RunAdbCommandWithProgress(const std::vector<std::string> &args, const std::function<void(const std::string&)> &on_chunk, const std::function<bool()> &abort_check, int idle_timeout_ms) {
    return ADBShell::adbExecWithProgress(BuildArgs(args), on_chunk, abort_check, idle_timeout_ms);
}

std::string ADBDevice::RunShellCommand(const std::string &command)
{
    EnsureConnection();
    return _adb_shell->shellCommand(command);
}

int ADBDevice::LastShellExitCode() const
{
    return _adb_shell ? _adb_shell->lastExitCode() : -1;
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

void ADBDevice::SyncPath()
{
    if (!_connected || !_adb_shell) {
        return;
    }
    try {
        std::string pwd_output = _adb_shell->shellCommand("pwd");
        std::string extracted = ExtractPathFromPwd(pwd_output);
        // Keep previous path if validation rejected pwd output (timeout / malformed marker) — don't blank a valid path.
        if (!extracted.empty()) {
            _current_path = extracted;
        }
    } catch (const std::exception& e) {
        // Ignore - keep current path
    }
}

wchar_t* ADBDevice::AllocateItemString(const std::string& s) {
    std::wstring ws = s.empty() ? std::wstring() : StrMB2Wide(s);
    size_t len = ws.length() + 1;
    wchar_t* buf = (wchar_t*)malloc(len * sizeof(wchar_t));
    if (!buf) {
        throw std::bad_alloc();
    }
    wcscpy(buf, ws.c_str());
    return buf;
}

bool ADBDevice::HaveFindPrintf()
{
    if (_find_printf < 0) {
        // Probe on a path that always exists and needs no directory traversal. -printf is a
        // GNU extension that toybox implements only in newer builds; where it is missing the
        // whole option is rejected, hence testing the option rather than the toybox version.
        // Probe every specifier the real command relies on, not just -printf: a build that
        // supports the option but not, say, %l would otherwise return silently empty records.
        std::string out = RunShellCommand(
            "find / -maxdepth 0 -printf '%s\\t%T@\\t%u\\t%g\\t%l\\t%M\\n' >/dev/null 2>&1"
            " && echo Y || echo N");
        // Only cache a definite answer. RunShellCommand yields "" on a shell timeout or restart,
        // and caching that as "no" would drop the session onto the slow path permanently.
        if (out.find('Y') != std::string::npos) {
            _find_printf = 1;
        } else if (out.find('N') != std::string::npos) {
            _find_printf = 0;
        }
        DBG("HaveFindPrintf: %d (probe returned '%s')\n", _find_printf, out.c_str());
        if (_find_printf < 0) {
            return false;   // undecided: use the portable path this time, retry the probe later
        }
    }
    return _find_printf == 1;
}

static constexpr const char* kDirectoryGlob =
    "for f in ./* ./.[!.]* ./..?*; do "
    "case \"$f\" in './*'|'./.[!.]*'|'./..?*') "
    "[ -e \"$f\" ] || [ -L \"$f\" ] || continue;; esac; ";

std::string ADBDevice::DirectoryEnum(const std::string &path, std::vector<PluginPanelItem> &files)
{

    if (!_connected || !_adb_shell) {
        throw std::runtime_error("ADB shell not connected");
    }

    if (path.find('\0') != std::string::npos) throw std::runtime_error("Invalid path");

    // Use the shell's directory entries for the names. Some Android paths deny getattr,
    // which means find can omit entries. The ls pass supplies metadata for older devices.
    const bool fast = HaveFindPrintf();
    std::ostringstream bulk_cmd;
    bulk_cmd << "if cd " << ADBUtils::ShellQuote(path) << " 2>/dev/null; then "
             << "printf '%s\\000' \"$PWD\"; ls -la; printf '\\000'; "
             << kDirectoryGlob
             << "printf 'N\\000%s\\000' \"${f#./}\"; ";
    if (!fast) {
        bulk_cmd << "if [ -L \"$f\" ] && [ -d \"$f\" ]; then printf 'Dd\\000%s\\000' \"${f#./}\"; fi; ";
    }
    bulk_cmd << "done; ";
    if (fast) {
        bulk_cmd << "find . -mindepth 1 -maxdepth 1 -printf 'B%M\\t%s\\t%T@\\t%u\\t%g\\t%l\\0%f\\0' 2>/dev/null; "
                 << "find -L . -mindepth 1 -maxdepth 1 -printf 'D%M\\0%f\\0' 2>/dev/null; ";
    }
    bulk_cmd << "printf 'E\\000\\000'; fi";

    const std::string bulk_output = RunShellCommand(bulk_cmd.str());
    const size_t path_end = bulk_output.find('\0');
    const size_t ls_end = path_end == std::string::npos ? path_end : bulk_output.find('\0', path_end + 1);
    if (ls_end == std::string::npos || bulk_output.empty() || bulk_output.front() != '/'
            || bulk_output.size() < 3 || bulk_output.compare(bulk_output.size() - 3, 3, "E\0\0", 3) != 0)
        throw std::runtime_error("Incomplete ADB directory listing");

    const std::string current_path = bulk_output.substr(0, path_end);
    const std::string_view records(bulk_output.data() + ls_end + 1, bulk_output.size() - ls_end - 1);
    auto for_each_record = [&](auto visit) {
        for (size_t pos = 0; pos < records.size();) {
            const size_t field_end = records.find('\0', pos);
            if (field_end == std::string_view::npos) break;
            const size_t name_end = records.find('\0', field_end + 1);
            if (name_end == std::string_view::npos) break;
            visit(records.substr(pos, field_end - pos), records.substr(field_end + 1, name_end - field_end - 1));
            pos = name_end + 1;
        }
    };

    std::vector<std::string> names;
    std::unordered_map<std::string_view, size_t> index;
    for_each_record([&](std::string_view field, std::string_view name) {
        if (field == "N" && IsLocalFileName(name) && index.emplace(name, names.size()).second)
            names.emplace_back(name);
    });

    files.clear();
    files.resize(names.size());
    std::istringstream ls_text(bulk_output.substr(path_end + 1, ls_end - path_end - 1));
    std::string ls_line;
    // Keep token views into this line. Large directories reuse the same small buffer.
    std::vector<std::pair<size_t, std::string_view>> tok;
    tok.reserve(12);
    while (std::getline(ls_text, ls_line)) {
        if (ls_line.find("Permission denied") != std::string::npos || ls_line.find("total") == 0)
            continue;
        if (ls_line.size() > 0 && ls_line[0] == '?')
            continue;

        // Tokenize remembering each token's offset: the name is taken verbatim from the
        // original line afterwards, so filenames containing runs of spaces survive.
        tok.clear();
        const std::string_view row(ls_line);
        for (size_t i = 0; i < ls_line.size(); ) {
            while (i < ls_line.size() && ls_line[i] == ' ') ++i;
            if (i >= ls_line.size()) break;
            const size_t b = i;
            while (i < ls_line.size() && ls_line[i] != ' ') ++i;
            tok.emplace_back(b, row.substr(b, i - b));
        }
        if (tok.size() < 3)
            continue;

        // Field count is not fixed at 7. Two shapes occur on real devices:
        //   char/block nodes split the size column in two - "crw-rw---- 1 system camera 490,   0 <date> <time> CAM"
        //   un-stattable entries have every field replaced by '?' and no date/time at all -
        //     "l?????????   ? ?      ?      ?      ? cache -> ?"
        // A blind 7-token split shifts the fields and yields filenames like "18:56 CAM" or "-> ?".
        const auto perms = tok[0].second;
        std::string_view links = "1", owner = "?", group = "?", size = "0";
        std::string date, time_str;
        size_t f = 1;
        if (perms.find('?') != std::string::npos) {
            // ls emits exactly five placeholders (nlink, owner, group, size, and a single '?'
            // where the date and time would be), so step over them by count. Scanning for the
            // first non-'?' token instead would misfire on a file actually named "?".
            if (tok.size() < 7)
                continue;
            f = 6;
        } else {
            if (tok.size() < 7)
                continue;
            links = tok[1].second; owner = tok[2].second; group = tok[3].second;
            size = tok[4].second;
            f = 5;
            if (!size.empty() && size.back() == ',') { size = "0"; ++f; } // skip device minor

            // The number of date tokens is device-dependent: toybox (Android 6+) prints ISO
            // "2026-08-06 18:57" (2 tokens), while busybox/toolbox - and any device with a
            // BusyBox ahead of toybox in PATH - prints "Aug  6 18:57" or "Aug  6 2024" (3).
            // So don't count tokens: the last metadata field is always either HH:MM or a bare
            // 4-digit year, so scan forward to that and treat everything before it as the date.
            size_t stamp = std::string::npos;
            for (size_t i = f; i < tok.size(); ++i) {
                const auto t = tok[i].second;
                if (t.find(':') != std::string::npos
                        || (t.size() == 4 && t.find_first_not_of("0123456789") == std::string::npos)) {
                    stamp = i;
                    break;
                }
            }
            if (stamp == std::string::npos || stamp + 1 >= tok.size())
                continue;
            for (size_t i = f; i < stamp; ++i) {
                if (!date.empty()) date += ' ';
                date += tok[i].second;
            }
            time_str = tok[stamp].second;
            f = stamp + 1;
        }
        if (f >= tok.size())
            continue;

        // Anchor one byte past the previous field rather than at the first non-space byte: ls
        // puts exactly one space before the name, so this keeps leading spaces that are part of
        // the filename (a file called " leading" must not become "leading", or every subsequent
        // path built from it points at something that does not exist).
        const size_t prev_end = tok[f - 1].first + tok[f - 1].second.size();
        if (prev_end + 1 > ls_line.size())
            continue;
        const auto rest = row.substr(prev_end + 1);

        auto filename = rest;
        std::string_view symlink_target;
        bool is_symlink = (perms[0] == 'l');
        if (is_symlink) {
            auto pos = rest.find(" -> ");
            if (pos != std::string::npos) {
                filename = rest.substr(0, pos);
                symlink_target = rest.substr(pos + 4);
            }
        }

        const auto exact = index.find(filename);
        if (exact == index.end()) continue;
        auto& item = files[exact->second];
        if (item.NumberOfLinks) continue;
        item.FindData.dwUnixMode = ADBUtils::LsPermsToMode(std::string(perms));
        item.FindData.dwFileAttributes = WINPORT(EvaluateAttributesA)(item.FindData.dwUnixMode, names[exact->second].c_str());
        if (perms[0] == 'd') item.FindData.dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;

        if (is_symlink) {
            item.Description = AllocateItemString(symlink_target.empty() ? "Symlink (no target)" : std::string(symlink_target));
        }

        std::from_chars(size.data(), size.data() + size.size(), item.FindData.nFileSize);
        item.FindData.nPhysicalSize = item.FindData.nFileSize;
        item.Owner = AllocateItemString(std::string(owner));
        item.Group = AllocateItemString(std::string(group));
        item.NumberOfLinks = 1;
        std::from_chars(links.data(), links.data() + links.size(), item.NumberOfLinks);

        // Rows for entries the shell may not stat carry no date at all. Leave their timestamps
        // zeroed rather than substituting "now", which made every SELinux-denied entry sort and
        // display as the newest file in the directory.
        if (!date.empty() && !time_str.empty()) {
            FILETIME ft{};
            time_t t = ParseLsDateTime(date, time_str);
            if (t) {
                ULARGE_INTEGER uli; uli.QuadPart = (t * 10000000ULL) + 116444736000000000ULL;
                ft.dwLowDateTime = uli.LowPart; ft.dwHighDateTime = uli.HighPart;
                item.FindData.ftCreationTime = item.FindData.ftLastAccessTime = item.FindData.ftLastWriteTime = ft;
            }
        }

    }

    for (size_t i = 0; i < names.size(); ++i) {
        auto& item = files[i];
        const auto& name = names[i];
        if (!item.NumberOfLinks) {
            item.NumberOfLinks = 1;
            item.FindData.dwFileAttributes = WINPORT(EvaluateAttributesA)(0, name.c_str());
        }
        item.FindData.lpwszFileName = AllocateItemString(name);
    }

    for_each_record([&](std::string_view field, std::string_view name) {
            if (field.empty() || field == "N") return;
            const auto found = index.find(name);
            if (found == index.end()) return;
        auto* item = &files[found->second];
        const char tag = field.front();
        const auto body = field.substr(1);

        if (tag == 'D') {
            // `find -L` followed the link, so this mode describes the *target*: a leading 'd'
            // means "symlink pointing at a directory", which is what makes it enterable.
            // Broken links are reported as the link itself and correctly stay non-directories.
            if (!body.empty() && body[0] == 'd') {
                item->FindData.dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
            }
            return;
        }
        if (tag != 'B') return;

        // The last field is the complete link target, including any tabs.
        std::string fld[5];
        size_t fp = 0;
        int n = 0;
        for (; n < 5; ++n) {
            const size_t tp = body.find('\t', fp);
            if (tp == std::string::npos) break;
            fld[n] = body.substr(fp, tp - fp);
            fp = tp + 1;
        }
        if (n < 5) return;
        const std::string target(body.substr(fp));
        item->FindData.dwUnixMode = ADBUtils::LsPermsToMode(fld[0]);
        item->FindData.dwFileAttributes =
            WINPORT(EvaluateAttributesA)(item->FindData.dwUnixMode, names[found->second].c_str());

        try {
            item->FindData.nFileSize = item->FindData.nPhysicalSize = std::stoull(fld[1]);
        } catch (...) {
        }

        // %T@ is "<epoch>.<nanoseconds>". Apply it even when it is 0 - some pseudo-filesystem
        // roots really do report epoch 0, and keeping ls's value for them would reintroduce
        // the timezone error this pass exists to remove.
        bool mtime_ok = false;
        time_t mt = 0;
        try {
            mt = (time_t)std::stoll(fld[2].substr(0, fld[2].find('.')));
            mtime_ok = true;
        } catch (...) {
            mtime_ok = false;
        }
        if (mtime_ok) {
            FILETIME ft{};
            ULARGE_INTEGER uli;
            uli.QuadPart = ((unsigned long long)mt * 10000000ULL) + 116444736000000000ULL;
            ft.dwLowDateTime = uli.LowPart;
            ft.dwHighDateTime = uli.HighPart;
            item->FindData.ftCreationTime = item->FindData.ftLastAccessTime =
                item->FindData.ftLastWriteTime = ft;
        }

        // Allocate first, then release the string the ls pass made: AllocateItemString may
        // throw, and freeing up front would leave a dangling pointer for the unwind path.
        if (!fld[3].empty()) {
            wchar_t *owner = AllocateItemString(fld[3]);
            free((void*)item->Owner);
            item->Owner = owner;
        }
        if (!fld[4].empty()) {
            wchar_t *group = AllocateItemString(fld[4]);
            free((void*)item->Group);
            item->Group = group;
        }
        if (!target.empty()) {
            wchar_t *descr = AllocateItemString(target);
            free((void*)item->Description);
            item->Description = descr;
        }
    });

    _current_path = current_path;
    return current_path;
}

std::string ADBDevice::ExtractPathFromPwd(const std::string &pwd_output)
{
    // Reject non-absolute / contaminated output: a broken session returns error text or shell fragments, which poison _current_path and leak into cmd-line echo.
    size_t end = pwd_output.size();
    while (end > 0 && (pwd_output[end - 1] == '\n' || pwd_output[end - 1] == '\r')) --end;
    if (end == 0 || pwd_output[0] != '/') {
        return "";
    }
    for (size_t i = 0; i < end; ++i) {
        char c = pwd_output[i];
        if (c == '\n' || c == '\r' || c == ';' || c == '`' || c == '$') {
            return "";
        }
    }
    return pwd_output.substr(0, end);
}

time_t ADBDevice::ParseLsDateTime(const std::string &date, const std::string &time_str) {
    struct tm timeinfo = {};
    time_t result = 0;
    int hour = 0, minute = 0, year_from_time = 0;

    // For entries older than ~6 months "ls -l" prints a 4-digit year where HH:MM normally
    // goes. Feeding that to "%d:%d" would set tm_hour=2024 and mktime() would then slide the
    // date weeks forward, so detect it and use it as the year at midnight instead.
    if (time_str.find(':') == std::string::npos) {
        year_from_time = atoi(time_str.c_str());
    } else {
        sscanf(time_str.c_str(), "%d:%d", &hour, &minute);
    }
    timeinfo.tm_hour = hour;
    timeinfo.tm_min = minute;
    timeinfo.tm_sec = 0;
    timeinfo.tm_isdst = -1; // let mktime work out DST rather than assuming standard time

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
            timeinfo.tm_year = year_from_time ? (year_from_time - 1900)
                                             : current_time->tm_year; // "MMM DD HH:MM" omits it
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
    if (path.find('\0') != std::string::npos) return false;
    std::string cd_command = "cd " + ADBUtils::ShellQuote(path)
        + " 2>/dev/null && printf '%s\\000' \"$PWD\"";
    std::string result = RunShellCommand(cd_command);
    if (result.size() < 2 || result.front() != '/' || result.find('\0') != result.size() - 1
            || LastShellExitCode() != 0) return false;
    result.pop_back();
    _current_path = std::move(result);
    return true;
}

int ADBDevice::TransferItem(const std::string& src, const std::string& dst, bool is_push, bool recursive,
                           const AdbProgressFn& on_progress,
                           const std::function<bool()>& abort_check)
{
    EnsureConnection();
    if (int err = ADBUtils::CheckConnection(_connected)) return err;

    std::vector<std::string> args = {is_push ? "push" : "pull"};
    // Always -p: besides driving the progress dialog, the continuous emits are what let the
    // runner tell "slow transfer" from "device fell off the bus" (see kTransferIdleTimeoutMs).
    args.push_back("-p");
    // Pull `-a` preserves timestamp+mode (verified `adb help`); push has no equivalent flag.
    if (!is_push) args.push_back("-a");
    args.push_back(src);
    args.push_back(dst);

    // adb can skip special objects yet exit successfully. Scan incrementally so a
    // skipped entry cannot be lost from the diagnostic tail of a long transfer.
    constexpr std::string_view skip_message = "skipping special file";
    constexpr size_t overlap = skip_message.size() - 1;
    std::string boundary;
    boundary.reserve(2 * overlap);
    bool skipped = false;
    ProgressParser parser(on_progress);
    if (on_progress) parser.start();
    const auto result = RunAdbCommandWithProgress(args, [&](const std::string& chunk) {
        if (!skipped) {
            boundary.append(chunk.data(), std::min(chunk.size(), overlap));
            skipped = chunk.find(skip_message) != chunk.npos || boundary.find(skip_message) != boundary.npos;
            if (chunk.size() >= overlap) boundary.assign(chunk, chunk.size() - overlap, overlap);
            else if (boundary.size() > overlap) boundary.erase(0, boundary.size() - overlap);
        }
        if (on_progress) parser(chunk);
    }, abort_check, ADBShell::kTransferIdleTimeoutMs);
    if (on_progress) parser.drain();
    if (ADBShell::lastPtyExit() == 0 && !skipped) {
        if (on_progress) parser.complete();
        return 0;
    }

    int errno_mapped = ADBShell::lastPtyExit() == ADBShell::kPtyExitKilled
        && abort_check && abort_check() ? ECANCELED : Str2Errno(result);
    if (errno_mapped == 0) errno_mapped = EIO;
#if defined(DEBUG) || defined(_DEBUG)
    const size_t tail_len = std::min<size_t>(result.size(), 400);
    const char* tail = result.c_str() + (result.size() - tail_len);
    DBG("TransferItem FAIL is_push=%d pty_exit=%d errno=%d src='%s' dst='%s' tail[%zu]='%.*s'\n",
        is_push, ADBShell::lastPtyExit(), errno_mapped, src.c_str(), dst.c_str(),
        tail_len, (int)tail_len, tail);
#endif
    return errno_mapped;
}

int ADBDevice::PullItem(const std::string& devicePath, const std::string& localPath, bool recursive,
                        const AdbProgressFn& on_progress, const std::function<bool()>& abort_check,
                        bool replace)
{
    if (abort_check && abort_check()) return ECANCELED;
    std::string target = localPath;
    struct stat previous{};
    if (replace && lstat(target.c_str(), &previous) == 0 && S_ISDIR(previous.st_mode)) {
        // adb pull treats an existing directory as a container. Keep its other entries.
        const auto start = devicePath.find_last_of('/') + 1;
        const auto name = devicePath.substr(start);
        if (!IsLocalFileName(name)) return EINVAL;
        target = ADBUtils::JoinPath(target, name);
        // An existing nested directory may contain unrelated files. Leave it for the user
        // to resolve via a different destination.
        if (lstat(target.c_str(), &previous) == 0 && S_ISDIR(previous.st_mode)) return EEXIST;
    }
    StagedFile stage(target, replace);
    if (!stage) return errno ? errno : EIO;
    if (int rc = TransferItem(devicePath, stage.Path(), false, recursive, on_progress, abort_check)) return rc;
    if (abort_check && abort_check()) return ECANCELED;
    struct stat st;
    if (lstat(stage.Path().c_str(), &st) != 0) return errno;
    if (recursive ? !S_ISDIR(st.st_mode) : !S_ISREG(st.st_mode)) return EIO;
    return stage.Publish() ? 0 : errno;
}

int ADBDevice::PullFile(const std::string& devicePath, const std::string& localPath,
                           const AdbProgressFn& on_progress, const std::function<bool()>& abort_check,
                           bool replace)
{
    return PullItem(devicePath, localPath, false, on_progress, abort_check, replace);
}

int ADBDevice::PushFile(const std::string &localPath, const std::string &devicePath) {
    return TransferItem(localPath, devicePath, true, false);
}

int ADBDevice::PushFile(const std::string &localPath, const std::string &devicePath, const AdbProgressFn &on_progress, const std::function<bool()> &abort_check) {
    return TransferItem(localPath, devicePath, true, false, on_progress, abort_check);
}

int ADBDevice::PullDirectory(const std::string& devicePath, const std::string& localPath,
                           const AdbProgressFn& on_progress, const std::function<bool()>& abort_check,
                           bool replace)
{
    return PullItem(devicePath, localPath, true, on_progress, abort_check, replace);
}

int ADBDevice::PushDirectory(const std::string &localPath, const std::string &devicePath) {
    return TransferItem(localPath, devicePath, true, true);
}

int ADBDevice::PushDirectory(const std::string &localPath, const std::string &devicePath, const AdbProgressFn &on_progress, const std::function<bool()> &abort_check) {
    return TransferItem(localPath, devicePath, true, true, on_progress, abort_check);
}



// Exit-code-first errno mapping: `result.empty()` alone confused warnings-on-success with real errors; the marker-protocol exit code is authoritative.
static int MutationResultToErrno(int exitCode, const std::string &result) {
    if (exitCode == 0) return 0;
    if (result.empty()) return EIO;
    return ADBDevice::Str2Errno(result);
}

int ADBDevice::DeleteFile(const std::string &devicePath) {
    EnsureConnection();
    if (int err = ADBUtils::CheckConnection(_connected)) return err;

    std::string command = "rm -- " + ADBUtils::ShellQuote(devicePath);
    std::string result = RunShellCommand(command);
    return MutationResultToErrno(LastShellExitCode(), result);
}

int ADBDevice::DeleteDirectory(const std::string &devicePath) {
    EnsureConnection();
    if (int err = ADBUtils::CheckConnection(_connected)) return err;

    std::string command = "rm -rf -- " + ADBUtils::ShellQuote(devicePath);
    std::string result = RunShellCommand(command);
    return MutationResultToErrno(LastShellExitCode(), result);
}

int ADBDevice::CreateDirectory(const std::string &devicePath) {
    EnsureConnection();
    if (int err = ADBUtils::CheckConnection(_connected)) return err;

    std::string command = "mkdir -p -- " + ADBUtils::ShellQuote(devicePath);
    std::string result = RunShellCommand(command);
    return MutationResultToErrno(LastShellExitCode(), result);
}

int ADBDevice::CopyRemote(const std::string &srcDevicePath, const std::string &dstDeviceDir) {
    EnsureConnection();
    if (int err = ADBUtils::CheckConnection(_connected)) return err;

    // cp -a (mode + symlinks) → fallback cp -Rp (mode-preserving, no -a needed on old toolbox).
    std::string command =
        "cp -a -- " + ADBUtils::ShellQuote(srcDevicePath) + " " + ADBUtils::ShellQuote(dstDeviceDir) +
        " 2>/dev/null || cp -Rp -- " + ADBUtils::ShellQuote(srcDevicePath) + " " + ADBUtils::ShellQuote(dstDeviceDir);
    std::string result = RunShellCommand(command);
    return MutationResultToErrno(LastShellExitCode(), result);
}

int ADBDevice::MoveRemote(const std::string &srcDevicePath, const std::string &dstDeviceDir) {
    EnsureConnection();
    if (int err = ADBUtils::CheckConnection(_connected)) return err;

    std::string command = "mv -- " + ADBUtils::ShellQuote(srcDevicePath) + " " + ADBUtils::ShellQuote(dstDeviceDir);
    std::string result = RunShellCommand(command);
    return MutationResultToErrno(LastShellExitCode(), result);
}

static std::string RemoteAsCommand(const std::string& src, const std::string& dst, bool move) {
    const auto source = ADBUtils::ShellQuote(src);
    const auto destination = ADBUtils::ShellQuote(dst);
    const auto absent = "[ ! -e " + destination + " ] && [ ! -L " + destination + " ]";
    const auto pair = " -- " + source + " " + destination;
    const auto command = move ? "mv" + pair
        : "cp -a" + pair + " 2>/dev/null || { " + absent + " && cp -Rp" + pair + "; }";
    return "if " + absent + "; then " + command + "; else echo 'File exists' >&2; false; fi";
}

int ADBDevice::CopyRemoteAs(const std::string &srcDevicePath, const std::string &dstDevicePath) {
    EnsureConnection();
    if (int err = ADBUtils::CheckConnection(_connected)) return err;

    const auto command = RemoteAsCommand(srcDevicePath, dstDevicePath, false);
    std::string result = RunShellCommand(command);
    return MutationResultToErrno(LastShellExitCode(), result);
}

int ADBDevice::MoveRemoteAs(const std::string &srcDevicePath, const std::string &dstDevicePath) {
    EnsureConnection();
    if (int err = ADBUtils::CheckConnection(_connected)) return err;

    const auto command = RemoteAsCommand(srcDevicePath, dstDevicePath, true);
    std::string result = RunShellCommand(command);
    return MutationResultToErrno(LastShellExitCode(), result);
}

int ADBDevice::CopyRemoteAs(const std::string &srcDevicePath, const std::string &dstDevicePath,
                            const std::function<bool()>& abort_check) {
    if (!abort_check) return CopyRemoteAs(srcDevicePath, dstDevicePath);
    if (int err = ADBUtils::CheckConnection(_connected)) return err;
    const auto sh = RemoteAsCommand(srcDevicePath, dstDevicePath, false);
    const std::string out = RunAdbCommandWithProgress({"shell", sh}, [](const std::string&){}, abort_check);
    if (abort_check()) return ECANCELED;
    return MutationResultToErrno(ADBShell::lastPtyExit(), out);
}

int ADBDevice::MoveRemoteAs(const std::string &srcDevicePath, const std::string &dstDevicePath,
                            const std::function<bool()>& abort_check) {
    if (!abort_check) return MoveRemoteAs(srcDevicePath, dstDevicePath);
    if (int err = ADBUtils::CheckConnection(_connected)) return err;
    const auto sh = RemoteAsCommand(srcDevicePath, dstDevicePath, true);
    const std::string out = RunAdbCommandWithProgress({"shell", sh}, [](const std::string&){}, abort_check);
    if (abort_check()) return ECANCELED;
    return MutationResultToErrno(ADBShell::lastPtyExit(), out);
}

int ADBDevice::CopyRemote(const std::string &srcDevicePath, const std::string &dstDeviceDir,
                          const std::function<bool()>& abort_check) {
    if (!abort_check) return CopyRemote(srcDevicePath, dstDeviceDir);
    if (int err = ADBUtils::CheckConnection(_connected)) return err;
    std::string sh =
        "cp -a -- " + ADBUtils::ShellQuote(srcDevicePath) + " " + ADBUtils::ShellQuote(dstDeviceDir) +
        " 2>/dev/null || cp -Rp -- " + ADBUtils::ShellQuote(srcDevicePath) + " " + ADBUtils::ShellQuote(dstDeviceDir);
    const std::string out = RunAdbCommandWithProgress({"shell", sh}, [](const std::string&){}, abort_check);
    if (abort_check()) return ECANCELED;
    return MutationResultToErrno(ADBShell::lastPtyExit(), out);
}

int ADBDevice::MoveRemote(const std::string &srcDevicePath, const std::string &dstDeviceDir,
                          const std::function<bool()>& abort_check) {
    if (!abort_check) return MoveRemote(srcDevicePath, dstDeviceDir);
    if (int err = ADBUtils::CheckConnection(_connected)) return err;
    std::string sh = "mv -- " + ADBUtils::ShellQuote(srcDevicePath) + " " + ADBUtils::ShellQuote(dstDeviceDir);
    const std::string out = RunAdbCommandWithProgress({"shell", sh}, [](const std::string&){}, abort_check);
    if (abort_check()) return ECANCELED;
    return MutationResultToErrno(ADBShell::lastPtyExit(), out);
}

int ADBDevice::SaveRemoteBackup(const std::string& destination, const std::string& aside) {
    EnsureConnection();
    if (int err = ADBUtils::CheckConnection(_connected)) return err;
    const auto backup = ADBUtils::ShellQuote(aside);
    // Stale backups can outlive a process/PID. Never overwrite one with mv.
    const auto output = RunShellCommand("[ ! -e " + backup + " ] && [ ! -L " + backup
        + " ] && mv -- " + ADBUtils::ShellQuote(destination) + " " + backup);
    return MutationResultToErrno(LastShellExitCode(), output);
}

int ADBDevice::RestoreRemoteBackup(const std::string& aside, const std::string& destination,
                                    bool remove_partial) {
    EnsureConnection();
    if (int err = ADBUtils::CheckConnection(_connected)) return err;
    const auto dst = ADBUtils::ShellQuote(destination);
    // mv alone would put the saved directory *inside* a partial destination.
    // Only copies/uploads may discard a partial output; a move can contain
    // the only remaining copy of some source files.
    std::string command = remove_partial ? "rm -rf -- " + dst + " && " : "";
    command += "[ ! -e " + dst + " ] && [ ! -L " + dst + " ] && mv -- "
        + ADBUtils::ShellQuote(aside) + " " + dst;
    const auto output = RunShellCommand(command);
    return MutationResultToErrno(LastShellExitCode(), output);
}

bool ADBDevice::FileExists(const std::string &devicePath) {
    EnsureConnection();
    if (!_connected) return false;

    const auto quoted = ADBUtils::ShellQuote(devicePath);
    const auto command = "[ -e " + quoted + " ] || [ -L " + quoted + " ]";
    RunShellCommand(command);
    return LastShellExitCode() == 0;
}

bool ADBDevice::IsDirectory(const std::string &devicePath) {
    EnsureConnection();
    if (!_connected) return false;
    std::string command = "test -d " + ADBUtils::ShellQuote(devicePath) + " && echo 1 || echo 0";
    std::string result = RunShellCommand(command);
    ADBUtils::TrimTrailingNewlines(result);
    while (!result.empty() && result.back() == ' ') result.pop_back();
    return result == "1";
}

void ADBDevice::ListDirNames(const std::string &devicePath, std::unordered_set<std::string>& out) {
    EnsureConnection();
    if (!_connected) return;
    const std::string command = "if cd " + ADBUtils::ShellQuote(devicePath) + " 2>/dev/null; then "
        + kDirectoryGlob + "printf '%s\\000' \"${f#./}\"; done; fi";
    const auto result = RunShellCommand(command);
    for (size_t pos = 0; pos < result.size();) {
        const auto end = result.find('\0', pos);
        if (end == result.npos) break;
        const std::string_view name(result.data() + pos, end - pos);
        if (IsLocalFileName(name)) out.emplace(name);
        pos = end + 1;
    }
}

void ADBDevice::BatchDirectoryFileSizes(const std::vector<std::string>& devicePaths,
                                         std::map<std::string, std::unordered_map<std::string, uint64_t>>& out) {
    EnsureConnection();
    if (!_connected || devicePaths.empty()) return;

    // Pre-create entries so empty top-level dirs (no files via -type f) still produce an entry.
    for (const auto& p : devicePaths) out[p];

    // Single `find` over all roots — one shell roundtrip; %p (full path) lets us prefix-match each file back to its root.
    std::string command = "find";
    for (const auto& p : devicePaths) command += " " + ADBUtils::ShellQuote(p);
    command += " -type f -printf '%s\\t%p\\n' 2>/dev/null";
    std::string result = RunShellCommand(command);

    // Longest-prefix first — handles the case where one input dir is nested inside another.
    std::vector<std::string> sorted = devicePaths;
    std::sort(sorted.begin(), sorted.end(),
              [](const std::string& a, const std::string& b) { return a.size() > b.size(); });

    size_t pos = 0;
    while (pos < result.size()) {
        size_t eol = result.find('\n', pos);
        std::string line = result.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        pos = (eol == std::string::npos) ? result.size() : eol + 1;
        if (line.empty()) continue;
        size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        const std::string size_str = line.substr(0, tab);
        std::string full_path = line.substr(tab + 1);
        while (!full_path.empty() && full_path.back() == '\r') full_path.pop_back();
        if (size_str.empty() || full_path.empty()) continue;

        for (const auto& dir : sorted) {
            if (full_path.size() > dir.size() + 1
                && full_path.compare(0, dir.size(), dir) == 0
                && full_path[dir.size()] == '/') {
                out[dir][full_path.substr(dir.size() + 1)] =
                    strtoull(size_str.c_str(), nullptr, 10);
                break;
            }
        }
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
