from common import check

check('Core copy preserves hard links and keeps an existing destination until publication', r'''
#include "far2l/src/CopyDestination.hpp"
#include <cassert>
#include <fstream>
#include <iterator>

#if defined(__APPLE__) || defined(__linux__)
#include <sys/xattr.h>
#endif
#include <cstdlib>
#ifdef __linux__
#include <endian.h>
#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>
#endif

static bool fail_mode = false;
int sdc_open(const char* path, int flags, ...) { return open(path, flags); }
int sdc_chown(const char* path, uid_t uid, gid_t gid) { return chown(path, uid, gid); }
int sdc_fchmod(int fd, mode_t mode) {
    if (fail_mode) { errno = EPERM; return -1; }
    return fchmod(fd, mode);
}
FDScope::~FDScope() { if (Valid()) close(Detach()); }
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
        assert(!copy.PublishFile("dest", false, false) && errno == EEXIST);
    }
    assert(Read("saved") == "previous" && Read("dest") == "new owner");
    assert(chmod("dest", 0640) == 0);
    struct stat previous{};
    assert(stat("dest", &previous) == 0);
    std::vector<gid_t> groups(getgroups(0, nullptr));
    if (!groups.empty()) {
        assert(getgroups(groups.size(), groups.data()) >= 0);
        for (auto group : groups) {
            if (group != previous.st_gid && chown("dest", previous.st_uid, group) == 0) break;
        }
    }
    assert(stat("dest", &previous) == 0);
#if defined(__APPLE__)
    assert(setxattr("dest", "user.test", "label", 5, 0, 0) == 0);
    assert(system("chmod +a 'everyone deny execute' dest") == 0);
#elif defined(__linux__)
    assert(setxattr("dest", "user.test", "label", 5, 0) == 0);
    struct { posix_acl_xattr_header header; posix_acl_xattr_entry entries[5]; } acl = {
        {htole32(POSIX_ACL_XATTR_VERSION)},
        {{htole16(ACL_USER_OBJ),htole16(6),UINT32_MAX},
         {htole16(ACL_USER),htole16(4),htole32(getuid()+1)},
         {htole16(ACL_GROUP_OBJ),htole16(4),UINT32_MAX},
         {htole16(ACL_MASK),htole16(4),UINT32_MAX},
         {htole16(ACL_OTHER),0,UINT32_MAX}}};
    assert(setxattr("dest", "system.posix_acl_access", &acl, sizeof(acl), 0) == 0);
#endif
    {
        CopyDestination copy;
        assert(copy.Prepare("source", "dest", false));
        std::ofstream(copy.StagingPath()) << "blocked";
        fail_mode = true;
        assert(!copy.PublishFile("dest", false, false));
        fail_mode = false;
        assert(Read("dest") == "new owner");
    }
    {
        CopyDestination copy;
        assert(copy.Prepare("source", "dest", false));
        std::ofstream(copy.StagingPath()) << "complete";
        assert(copy.PublishFile("dest", false, false));
    }
    assert(Read("dest") == "complete");
    struct stat st{}; assert(stat("dest",&st)==0 && (st.st_mode & 0777)==0640);
    assert(st.st_gid == previous.st_gid && st.st_uid == previous.st_uid);
    char value[5];
#if defined(__APPLE__)
    assert(getxattr("dest", "user.test", value, 5, 0, 0) == 5);
    acl_t acl=acl_get_file("dest", ACL_TYPE_EXTENDED); assert(acl);
    char* text=acl_to_text(acl,nullptr);assert(text && strstr(text,"deny") && strstr(text,"execute"));
    acl_free(text);acl_free(acl);
#elif defined(__linux__)
    assert(getxattr("dest", "user.test", value, 5) == 5);
    decltype(acl) actual_acl{};
    assert(getxattr("dest","system.posix_acl_access",&actual_acl,sizeof(actual_acl)) == sizeof(acl));
    assert(!memcmp(&acl,&actual_acl,sizeof(acl)));
#endif
#if defined(__APPLE__) || defined(__linux__)
    assert(!memcmp(value,"label",5));
#endif
    {
        CopyDestination copy;
        assert(copy.Prepare("source", "dest", false));
        std::ofstream(copy.StagingPath()) << "readonly";
        assert(chmod(copy.StagingPath().c_str(), 0444) == 0);
        assert(copy.PublishFile("dest", true, true));
        assert(stat("dest", &st) == 0 && (st.st_mode & 0777) == 0444);
    }
}
''')
