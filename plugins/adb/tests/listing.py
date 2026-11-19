from pathlib import Path
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'testing/perf-regressions'))
from common import check, section

device = 'plugins/adb/src/ADBDevice.cpp'
quote = section(device, 'std::string ShellQuote(', 'std::string JoinPath(')
listing = section(device, 'static constexpr const char* kDirectoryGlob', 'std::string ADBDevice::ExtractPathFromPwd')
remote = section(device, 'static std::string RemoteAsCommand(', 'int ADBDevice::CopyRemoteAs(')
check('ADB preserves exact filenames and refuses an unapproved remote replacement', r'''
#include "utils/src/StagedFile.cpp"
#include "far2l/far2sdk/farplug-wide.h"
#include <cassert>
#include <charconv>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#undef WINPORT
#define WINPORT(name) Test##name
DWORD TestEvaluateAttributesA(mode_t mode, const char*) {
    return S_ISDIR(mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}
namespace ADBUtils {
''' + quote + r'''
mode_t LsPermsToMode(const std::string& perms) {
    if (perms[0] == 'd') return S_IFDIR;
    return perms[0] == 'l' ? S_IFLNK : S_IFREG;
}
}
struct ADBDevice {
    bool _connected = true, _adb_shell = true;
    std::string _current_path;
    static bool HaveFindPrintf() { return false; }
    static time_t ParseLsDateTime(const std::string&, const std::string&) { return 1; }
    static wchar_t* AllocateItemString(const std::string& text) {
        return wcsdup(std::wstring(text.begin(), text.end()).c_str());
    }
    static std::string RunShellCommand(const std::string& command) {
        FILE* pipe = popen(command.c_str(), "r");
        assert(pipe);
        std::string output;
        char buffer[512];
        for (size_t count; (count = fread(buffer, 1, sizeof(buffer), pipe)) != 0;)
            output.append(buffer, count);
        pclose(pipe);
        return output;
    }
    std::string DirectoryEnum(const std::string&, std::vector<PluginPanelItem>&);
};
''' + listing + remote + r'''
int main(int argc, char** argv) {
    assert(argc == 2 && chdir(argv[1]) == 0);
    assert(mkdir("folder", 0700) == 0);
    std::ofstream("folder/line\nbreak") << "newline";
    std::ofstream("folder/ \\ literal") << "literal";
    std::ofstream("folder/.hidden") << "hidden";
    std::ofstream("folder/arrow -> ordinary") << "regular";
    assert(mkdir("folder/target", 0700) == 0);
    assert(symlink("target", "folder/link -> name") == 0);
    std::vector<PluginPanelItem> files;
    ADBDevice adb;
    assert(adb.DirectoryEnum(std::string(argv[1]) + "/folder", files) == std::string(argv[1]) + "/folder");
    std::unordered_set<std::wstring> names;
    for (auto& item : files) {
        assert(names.insert(item.FindData.lpwszFileName).second);
        if (std::wstring_view(item.FindData.lpwszFileName) == L"link -> name")
            assert(item.FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        free((void*)item.FindData.lpwszFileName);
        free((void*)item.Owner);
        free((void*)item.Group);
        free((void*)item.Description);
    }
    assert((names == std::unordered_set<std::wstring>{
        L".hidden", L"target", L"link -> name", L" \\ literal", L"arrow -> ordinary", L"line\nbreak"
    }));
    std::ofstream("original") << "keep";
    std::ofstream("source") << "incoming";
    assert(system((RemoteAsCommand("source", "original", false) + " 2>/dev/null").c_str()) != 0);
    assert(system((RemoteAsCommand("source", "original", true) + " 2>/dev/null").c_str()) != 0);
    std::string original, source;
    std::ifstream("original") >> original;
    std::ifstream("source") >> source;
    assert(original == "keep" && source == "incoming");
}
''')
