// g++ ./TestPath_test.cpp -o TestPath_test -I../include

#include "TestPath.cpp"
#include <cassert>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

struct TestFixture {
    const char* nonexistent_file = "dummy_nonexistent.txt";
    const char* regular_file = "dummy_regular.txt";
    const char* executable_file = "dummy_executable.sh";
    const char* dir_path = "dummy_dir";

    TestFixture() {
        unlink(nonexistent_file);
        unlink(regular_file);
        unlink(executable_file);
        rmdir(dir_path);

        int fd1 = open(regular_file, O_CREAT | O_WRONLY, 0644);
        assert(fd1 != -1);
        close(fd1);

        int fd2 = open(executable_file, O_CREAT | O_WRONLY, 0755);
        assert(fd2 != -1);
        close(fd2);

        int res = mkdir(dir_path, 0755);
        assert(res == 0);
    }

    ~TestFixture() {
        unlink(regular_file);
        unlink(executable_file);
        rmdir(dir_path);
    }
};

int main() {
    TestFixture fixture;

    // Test nonexistent
    TestPath tp_nonexistent(fixture.nonexistent_file);
    assert(tp_nonexistent.Exists() == false);
    assert(tp_nonexistent.Directory() == false);
    assert(tp_nonexistent.Regular() == false);
    assert(tp_nonexistent.Executable() == false);

    // Test regular non-executable
    TestPath tp_regular(fixture.regular_file);
    assert(tp_regular.Exists() == true);
    assert(tp_regular.Directory() == false);
    assert(tp_regular.Regular() == true);
    assert(tp_regular.Executable() == false);

    // Test executable file
    TestPath tp_executable(fixture.executable_file);
    assert(tp_executable.Exists() == true);
    assert(tp_executable.Directory() == false);
    assert(tp_executable.Regular() == true);
    assert(tp_executable.Executable() == true);

    // Test directory
    TestPath tp_dir(fixture.dir_path);
    assert(tp_dir.Exists() == true);
    assert(tp_dir.Directory() == true);
    assert(tp_dir.Regular() == false);
    assert(tp_dir.Executable() == false);

    return 0;
}
