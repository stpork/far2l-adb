from common import section, check
code=section('far2l/src/viewer.cpp','\t\tif (Count == 1 && !Raw &&','\n\t\tViewFile.SetPointer(Ptr + ReadSize);')
check('UTF-16 single-character reads',r'''
#include <algorithm>
#include <vector>
#include <cassert>
#include <cstdint>
using DWORD=unsigned;using LPBYTE=unsigned char*;
enum {CP_UTF16BE=1201,CP_UTF16LE=1200,WCHAR_REPLACEMENT=0xfffd};
struct File {std::vector<unsigned char> bytes;size_t ptr=0;
LPBYTE ViewBytesAt(size_t p,DWORD &n){n=std::min<size_t>(n,bytes.size()-p);return n ? bytes.data()+p : nullptr;}
void SetPointer(size_t p){ptr=p;}};
struct Viewer {File ViewFile;struct {int CodePage=CP_UTF16BE;} VM;
int read(wchar_t *Buf){int Count=1;bool Raw=false;auto Ptr=ViewFile.ptr;DWORD ReadSize=2;
LPBYTE View=ViewFile.ViewBytesAt(Ptr,ReadSize);
'''+code+r'''
return -1;}};
void test(std::vector<unsigned char> bytes,std::vector<wchar_t> expected,int cp,size_t offset=0){
Viewer v;v.VM.CodePage=cp;v.ViewFile.bytes=bytes;v.ViewFile.ptr=offset;
for(auto c:expected){wchar_t out=0;assert(v.read(&out)==1 && out==c);}
wchar_t out;assert(v.read(&out)==0);assert(v.ViewFile.ptr==bytes.size());}
int main(){test({0xd8,0x3d,0xde,0},{0x1f600},CP_UTF16BE);test({0x3d,0xd8,0,0xde},{0x1f600},CP_UTF16LE);
test({0,0xd8,0,0x41},{0xd8,L'A'},CP_UTF16BE);test({0xdc,0,0,0x41},{0xfffd,L'A'},CP_UTF16BE);
test({0xd8,0,0,0x41},{0xfffd,L'A'},CP_UTF16BE);test({0xd8,0,0},{0xfffd,0xfffd},CP_UTF16BE);
test({0},{0xfffd},CP_UTF16LE);test({},{},CP_UTF16BE);test({0,0xd8,0x3d,0xde,0},{0x1f600},CP_UTF16BE,1);
}
''')
