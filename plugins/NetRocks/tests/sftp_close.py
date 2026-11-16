from pathlib import Path
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'testing/perf-regressions'))
from common import section, check

prefix = r'''
#include <string>
#include <stdexcept>
#include <memory>
#include <algorithm>
#include <cassert>
#include <sys/types.h>
struct ProtocolError:std::runtime_error {ProtocolError(const char* a,const char*) : std::runtime_error(a) {}};
struct IFileWriter {virtual void Write(const void*,size_t)=0;virtual void WriteComplete()=0;virtual ~IFileWriter()=default;};
constexpr int SSH_OK=0;
static int closes=0;
static bool fail_close=false;
int sftp_close(int file){assert(file==42);++closes;return fail_close?-1:SSH_OK;}
void FileDeleter(int file){if(file)sftp_close(file);}
const char* ssh_get_error(int){return "close error";}
ssize_t sftp_write(int,const void*,size_t size){return size;}
struct SFTPConnection {int ssh=1;size_t max_write_block=32768;};
'''
resource = section('plugins/NetRocks/src/Protocol/SSH/SSHConnection.h', '#define RESOURCE_CONTAINER(',
                   'void SSHSessionDeleter(')
base = r'''
RESOURCE_CONTAINER(SFTPFile,int,FileDeleter);
struct SFTPFileIO {
    std::shared_ptr<SFTPConnection> _conn;
    SFTPFile _file{42};
    SFTPFileIO(std::shared_ptr<SFTPConnection>& c,const std::string&,int,mode_t,unsigned long long):_conn(c){}
};
'''
writer = section('plugins/NetRocks/src/Protocol/SSH/ProtocolSFTP.cpp', 'class SFTPFileWriter :',
                  'std::shared_ptr<IFileReader> ProtocolSFTP::FileGet(')
check('SFTP close errors propagate and release each handle once', prefix + resource + base + writer + r'''
int main(){
    auto connection=std::make_shared<SFTPConnection>();
    {SFTPFileWriter writer(connection,"file",0,0600,0);writer.WriteComplete();writer.WriteComplete();}
    assert(closes==1);
    fail_close=true;bool failed=false;
    {SFTPFileWriter writer(connection,"file",0,0600,0);try{writer.WriteComplete();}catch(const ProtocolError&){failed=true;}}
    assert(failed && closes==2);
}
''')
