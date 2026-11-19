from pathlib import Path
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'testing/perf-regressions'))
from common import check, section

download = section('plugins/mtp/src/LibMtpBackend.cpp', 'Status LibMtpBackend::Download(', 'Status LibMtpBackend::Upload(')
check('MTP failed and cancelled downloads retain the original destination', r'''
#include "utils/src/StagedFile.cpp"
#include "plugins/mtp/src/Types.h"
#include <cassert>
#include <fstream>
#include <cstring>
#include <functional>
#include <mutex>
using namespace proto;
using ObjectHandle = uint32_t;
struct ProgressCtx {
    std::function<void(uint64_t, uint64_t)> callback;
    const CancellationToken* cancel;
};
constexpr int ProgressTrampoline = 0;
static bool fail = false;
static bool cancelled = false;
static CancellationSource source;
int LIBMTP_Get_File_To_File_Descriptor(void*, ObjectHandle, int fd, int, void*) {
    assert(write(fd, "download", 8) == 8);
    if (cancelled) source.Cancel();
    return fail ? -1 : 0;
}
struct LibMtpBackend {
    std::recursive_mutex _mtx;
    void* _device = this;
    void ClearErrors() {}
    void ReopenAfterCancelLocked() {}
    Status DrainErrors(const char*) { return Status::Failure(ErrorCode::Io, "interrupted"); }
    Status Download(ObjectHandle, const std::string&, std::function<void(uint64_t, uint64_t)>, const CancellationToken&);
};
static std::string Read(const char* path) {
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), {}};
}
''' + download + r'''
int main(int argc, char** argv) {
    assert(argc == 2 && chdir(argv[1]) == 0);
    LibMtpBackend backend;
    std::ofstream("dest") << "original";
    assert(!backend.Download(1, "dest", {}, source.Token()).ok && Read("dest") == "original");
    {
        StagedFile stage("dest", true);
        assert(stage);
        fail = true;
        auto result = backend.Download(1, stage.Path(), {}, source.Token());
        assert(!result.ok && access(stage.Path().c_str(), F_OK) != 0);
    }
    assert(Read("dest") == "original");
    {
        StagedFile stage("dest", true);
        fail = false;
        cancelled = true;
        auto result = backend.Download(1, stage.Path(), {}, source.Token());
        assert(!result.ok && result.code == ErrorCode::Cancelled);
    }
    assert(Read("dest") == "original");
    {
        StagedFile stage("dest", true);
        cancelled = false;
        CancellationSource next;
        assert(backend.Download(1, stage.Path(), {}, next.Token()).ok);
        assert(stage.Publish());
    }
    assert(Read("dest") == "download");
    {
        StagedFile stage("dest", true);
        std::ofstream(stage.Path()) << "replacement";
        assert(rename("dest", "retained") == 0);
        std::ofstream("dest") << "different file";
        assert(!stage.Publish() && errno == EEXIST);
    }
    assert(Read("retained") == "download" && Read("dest") == "different file");
    for (auto name : {"", ".", "..", "part/name"}) assert(!IsLocalFileName(name));
    assert(!IsLocalFileName(std::string_view("a\0b", 3)));
    assert(IsLocalFileName("O'Brien \\ files"));
}
''')
