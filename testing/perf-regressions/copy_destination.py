from common import check

check('Core copy preserves hard links and keeps an existing destination until publication', r'''
#include "far2l/src/CopyDestination.hpp"
#include <cassert>
#include <fstream>
#include <iterator>

int sdc_stat(const char* p, struct stat* st) { return stat(p, st); }
int sdc_lstat(const char* p, struct stat* st) { return lstat(p, st); }
int sdc_unlink(const char* p) { return unlink(p); }
int sdc_rmdir(const char* p) { return rmdir(p); }
int sdc_mkdir(const char* p, mode_t mode) { return mkdir(p, mode); }
int sdc_chmod(const char* p, mode_t mode) { return chmod(p, mode); }
int sdc_rename(const char* from, const char* to) { return rename(from, to); }

static std::string Read(const char* path) {
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), {}};
}

int main(int argc, char** argv) {
    assert(argc == 2 && chdir(argv[1]) == 0);
    std::ofstream("source") << "source";
    std::ofstream("dest") << "previous";
    assert(link("source", "alias") == 0);
    assert(symlink("source", "symlink") == 0);
    for (auto alias : {"alias", "symlink"}) {
        for (bool in_place : {false, true}) {
            CopyDestination copy;
            assert(!copy.Prepare("source", alias, in_place) && errno == EINVAL);
        }
    }
    assert(Read("source") == "source");
    {
        CopyDestination copy;
        assert(copy.Prepare("source", "dest", false) && copy.create_new);
        std::ofstream(copy.StagingPath()) << "partial";
    }
    assert(Read("dest") == "previous");
    {
        CopyDestination copy;
        assert(copy.Prepare("source", "dest", false));
        std::ofstream(copy.StagingPath()) << "complete";
        assert(rename("dest", "saved") == 0);
        std::ofstream("dest") << "new owner";
        assert(!copy.Publish("dest", false) && errno == EEXIST);
    }
    assert(Read("saved") == "previous" && Read("dest") == "new owner");
    {
        CopyDestination copy;
        assert(copy.Prepare("source", "dest", false));
        std::ofstream(copy.StagingPath()) << "complete";
        assert(copy.Publish("dest", false));
    }
    assert(Read("dest") == "complete");
}
''')
