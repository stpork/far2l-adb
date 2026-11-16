from pathlib import Path
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'testing/perf-regressions'))
from common import section, check

prefix = r'''
#include <string>
#include <stdexcept>
#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
struct ProtocolError:std::runtime_error {
    ProtocolError(const char* message,int):std::runtime_error(message){}
};
struct IFileReader {virtual size_t Read(void*,size_t)=0;virtual ~IFileReader()=default;};
struct IFileWriter {virtual void Write(const void*,size_t)=0;virtual void WriteComplete()=0;virtual ~IFileWriter()=default;};
static bool close_failure = false;
static int closes = 0;
int TestClose(int fd) {
    ++closes;
    const int rc = close(fd);
    if (close_failure) { errno=EIO; return -1; }
    return rc;
}
#define close TestClose
#define API(name) name
void CheckedCloseFD(int& fd) { if(fd!=-1) {TestClose(fd);fd=-1;} }
struct HostLocal { bool IsSameFile(const std::string&, const std::string&); };
'''
identity = section('plugins/NetRocks/src/Host/HostLocal.cpp', 'bool HostLocal::IsSameFile(',
                   'std::shared_ptr<IDirectoryEnumer> HostLocal::DirectoryEnum(')
fileio = section('plugins/NetRocks/src/Host/HostLocal.cpp', 'class HostLocalFileIO :',
                 'std::shared_ptr<IFileReader> HostLocal::FileGet(')
check('Local same-object aliases and checked close', prefix + identity + fileio + r'''
int main(int argc, char** argv) {
    std::string a=std::string(argv[1])+"/source", b=a+".hard", c=a+".sym";
    {HostLocalFileIO writer(a,0,O_CREAT|O_TRUNC|O_RDWR,0600);writer.Write("data",4);writer.WriteComplete();writer.WriteComplete();}
    assert(closes==1);
    assert(link(a.c_str(),b.c_str())==0 && symlink(a.c_str(),c.c_str())==0);
    HostLocal host;
    assert(host.IsSameFile(a,a) && host.IsSameFile(a,b) && host.IsSameFile(a,c));
    assert(!host.IsSameFile(a,a+".missing"));
    auto dangling=a+".dangling";
    assert(symlink("missing",dangling.c_str())==0);
    assert(host.IsSameFile(dangling,dangling));
    assert(!host.IsSameFile(dangling,a));
    bool failed=false;close_failure=true;
    {HostLocalFileIO writer(a,0,O_RDWR,0600);try{writer.WriteComplete();}catch(const ProtocolError&){failed=true;}}
    assert(failed && closes==2); // A failed close must not close the same fd again.
}
''')
