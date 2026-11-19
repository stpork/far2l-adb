from pathlib import Path
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'testing/perf-regressions'))
from common import check, section

item = section('plugins/adb/src/ADBDevice.cpp', 'int ADBDevice::PullItem(', 'int ADBDevice::PullFile(')
paths = section('plugins/adb/src/ADBDevice.cpp', 'std::string JoinPath(', 'void TrimTrailingNewlines(')
check('ADB pull preserves existing files on failure, cancel and late collisions', r'''
#include "utils/src/StagedFile.cpp"
#include <cassert>
#include <fstream>
#include <functional>
#include <iterator>
namespace ADBUtils {
''' + paths + r'''
}
using AdbProgressFn = std::function<void(int, const std::string&)>;
struct ADBDevice {
    bool fail = false, cancel = false, collide = false;
    int PullItem(const std::string&, const std::string&, bool,
                 const AdbProgressFn&, const std::function<bool()>&, bool);
    int TransferItem(const std::string&, const std::string& path, bool, bool recursive,
                     const AdbProgressFn&, const std::function<bool()>&) {
        if (recursive) {
            assert(mkdir(path.c_str(), 0700) == 0);
            std::ofstream(path + "/file") << "download";
        } else {
            std::ofstream(path) << "download";
        }
        if (collide) std::ofstream("late") << "new owner";
        return fail ? EIO : 0;
    }
};
static std::string Read(const char* path) {
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), {}};
}
''' + item + r'''
int main(int argc, char** argv) {
    assert(argc == 2 && chdir(argv[1]) == 0);
    ADBDevice adb;
    std::ofstream("file") << "original";
    adb.fail = true;
    assert(adb.PullItem("/phone/file", "file", false, {}, {}, true) == EIO);
    assert(Read("file") == "original");
    adb.fail = false;
    int callbacks = 0;
    assert(adb.PullItem("/phone/file", "file", false, {}, [&]{ return ++callbacks > 1; }, true) == ECANCELED);
    assert(Read("file") == "original");
    assert(adb.PullItem("/phone/file", "file", false, {}, {}, false) == EEXIST);
    assert(Read("file") == "original");
    adb.collide = true;
    assert(adb.PullItem("/phone/file", "late", false, {}, {}, false) == EEXIST);
    assert(Read("late") == "new owner");
    adb.collide = false;
    assert(adb.PullItem("/phone/file", "file", false, {}, {}, true) == 0);
    assert(Read("file") == "download");

    assert(mkdir("container", 0700) == 0);
    std::ofstream("container/unrelated") << "keep";
    assert(adb.PullItem("/phone/sub", "container", true, {}, {}, true) == 0);
    assert(Read("container/sub/file") == "download");
    assert(Read("container/unrelated") == "keep");
    assert(adb.PullItem("/phone/sub", "container", true, {}, {}, true) == EEXIST);
    assert(Read("container/sub/file") == "download");
    assert(ADBUtils::JoinPath("/device/path", "\\literal") == "/device/path/\\literal");
}
''')
