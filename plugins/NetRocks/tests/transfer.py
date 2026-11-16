from pathlib import Path
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'testing/perf-regressions'))
from common import ROOT, section, check
prefix = r'''
#include <algorithm>
#include <vector>
#include <memory>
#include <string>
#include <stdexcept>
#include <chrono>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <sys/stat.h>
#include "plugins/NetRocks/src/Op/Utils/IOBuffer.cpp"
#define BUFFER_SIZE_GRANULARITY 32768
#define EXTRA_NEEDED_MODE (S_IRUSR|S_IWUSR)
int g_netrocks_verbosity=0;
enum {XD_UPLOAD,XD_DOWNLOAD,WEK_UPLOAD,WEK_DOWNLOAD,WEK_CROSSLOAD,WEA_SKIP,WEA_RETRY};
struct ProtocolError:std::runtime_error {using std::runtime_error::runtime_error;};
struct AbortError:std::runtime_error {AbortError():std::runtime_error("abort"){} };
struct FileInformation {unsigned long long size=131072; unsigned mode=0644;};
struct State {
    struct {unsigned long long file_complete=0,file_total=131072,all_total=131072,all_complete=0,count_skips=0,count_retries=0;} stats;
};
struct ProgressStateUpdate {ProgressStateUpdate(State&){} };
std::chrono::milliseconds TimeMSNow(){static int t=0;return std::chrono::milliseconds(++t);}
struct Errors {
    void ResetAutoRetryDelay(){}
    int Query(State&,int,const char*,const std::string&,const std::string&){return WEA_RETRY;}
};
struct IFileReader {virtual size_t Read(void*,size_t)=0; virtual ~IFileReader()=default;};
struct IFileWriter {virtual void Write(const void*,size_t)=0;virtual void WriteComplete()=0;virtual ~IFileWriter()=default;};
struct MemoryReader:IFileReader {
    std::vector<char>& data;size_t pos;
    MemoryReader(std::vector<char>& d,size_t p):data(d),pos(p){}
    size_t Read(void* p,size_t n) override {n=std::min(n,data.size()-pos);memcpy(p,data.data()+pos,n);pos+=n;return n;}
};
struct MemoryWriter:IFileWriter {
    std::vector<char>& data;size_t pos;int& calls;bool& fault;
    MemoryWriter(std::vector<char>& d,size_t p,int& c,bool& f):data(d),pos(p),calls(c),fault(f){}
    void Write(const void* p,size_t n) override {
        ++calls;
        if(fault && calls==2){fault=false;throw std::runtime_error("delayed partial-write error");}
        if(fault && calls==1)n/=2;
        data.resize(pos+n);memcpy(data.data()+pos,p,n);pos+=n;
    }
    void WriteComplete() override {}
};
struct IHost {
    std::vector<char> data;int calls=0;bool fault=false;
    bool IsSameFile(const std::string&a,const std::string&b){return a==b;}
    void ReInitialize(){}
    std::string SiteName(){return "mock";}
    unsigned long long GetSize(const std::string&){return data.size();}
    std::shared_ptr<IFileReader> FileGet(const std::string&,unsigned long long p){return std::make_shared<MemoryReader>(data,p);}
    std::shared_ptr<IFileWriter> FilePut(const std::string&,mode_t,unsigned long long,unsigned long long p){if(!p)data.clear();return std::make_shared<MemoryWriter>(data,p,calls,fault);}
};
struct OpXfer {
    State _state;
    bool _same_host=false;
    std::shared_ptr<IHost> _base_host=std::make_shared<IHost>(),_dst_host=std::make_shared<IHost>();
    std::shared_ptr<Errors> _wea_state=std::make_shared<Errors>();
    IOBuffer _io_buf{65536,32768,16777216};
    int _direction=XD_UPLOAD;
    void CheckDistinctFiles(const std::string&,const std::string&);
    void EnsureProgressConsistency();
    bool FileCopyLoop(const std::string&,const std::string&,FileInformation&);
};
'''
main = r'''
int main() {
    OpXfer x;FileInformation info;
    x._base_host->data.assign(info.size,'a');x._dst_host->fault=true;
    bool ok=x.FileCopyLoop("src","dst",info);
    printf("Retry: success=%d, actual_bytes=%zu, file_complete=%llu, all_complete=%llu, all_total=%llu\n",ok,x._dst_host->data.size(),x._state.stats.file_complete,x._state.stats.all_complete,x._state.stats.all_total);
    assert(ok && x._dst_host->data==x._base_host->data && x._state.stats.all_complete==131072);
    OpXfer y;FileInformation same;
    y._base_host->data.assign(same.size,'b');y._dst_host=y._base_host;
    y._same_host=true;
    bool rejected=false;
    try { y.CheckDistinctFiles("same", "same"); y.FileCopyLoop("same","same",same); } catch (const ProtocolError&) {rejected=true;}
    assert(rejected && y._base_host->data.size()==same.size);
}
'''
check('Transfer retries and same-file source protection',prefix+section('plugins/NetRocks/src/Op/OpXfer.cpp','void OpXfer::CheckDistinctFiles(', 'void OpXfer::DirectoryCopy(')+main)
