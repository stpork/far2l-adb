prefix=r'''
#include <algorithm>
#include <vector>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <cstdint>
using DWORD=uint32_t; using INT64=int64_t;
enum { FILE_FLAG_NO_BUFFERING=1, FILE_CURRENT=1 };
namespace Msg { const wchar_t *CopyReadError=L"read",*CopyWriteError=L"write"; }
struct ErrnoSaver:std::runtime_error { ErrnoSaver():std::runtime_error("I/O"){} };
template<class T> T AlignPageUp(T n) { return (n+4095)&~T(4095); }
uint64_t CurCopiedSize=0;
struct FakeFile {
    std::vector<unsigned char> bytes;
    int64_t pos=0; size_t write_limit=SIZE_MAX;
    bool Read(void *p,DWORD n,DWORD *out) {
        *out=std::min<size_t>(n,bytes.size()-pos);
        memcpy(p,bytes.data()+pos,*out); pos+=*out; return true;
    }
    bool Write(const void *p,DWORD n,DWORD *out) {
        *out=std::min<size_t>(n,write_limit);
        bytes.resize(std::max<size_t>(bytes.size(),pos+*out));
        memcpy(bytes.data()+pos,p,*out);pos+=*out;return true;
    }
    bool SetPointer(INT64 delta,void*,int) {pos+=delta;return pos>=0;}
    bool SetEnd(){bytes.resize(pos);return true;}
};
struct ShellFileTransfer {
    FakeFile _SrcFile,_DestFile;
    unsigned char storage[65536]={};
    struct {char *Ptr; DWORD Size;} _CopyBuffer{(char*)storage,65536};
    struct {bool SPARSEFILES=false,USECOW=false;} _Flags;
    DWORD _DstFlags=0;
    bool _LastWriteWasHole=false;
    const wchar_t *_SrcName=L"src",*_strDestName=L"dst";
    void RetryCancel(const wchar_t*,const wchar_t*) {throw ErrnoSaver();}
    DWORD PieceCopy(); DWORD PieceWrite(const void*,DWORD); DWORD PieceWriteHole(DWORD);
};
'''
