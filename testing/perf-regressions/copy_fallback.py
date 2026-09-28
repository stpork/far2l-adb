from common import section, check
block=section('far2l/src/copy.cpp','#if defined(COW_SUPPORTED) && defined(__linux__)','\n\tDWORD BytesRead, BytesWritten;')
check('Per-transfer fast-copy fallback',r'''
#include <cerrno>
#include <cstdio>
#include <cassert>
#include <stdexcept>
#include <cstdint>
#include <initializer_list>
#define COW_SUPPORTED 1
#define __linux__ 1
#define copy_file_range fake_copy
using DWORD=uint32_t;
int error_code=0,calls=0;
long fake_copy(int,void*,int,void*,unsigned,int){++calls;errno=error_code;return error_code ? -1 : 7;}
namespace Msg {const wchar_t *CopyWriteError=L"write";}
struct Transfer {
bool _UseCOW=true;unsigned fallbacks=0;
struct {int Descriptor(){return 1;}} _SrcFile,_DestFile;
struct {unsigned Size=65536;} _CopyBuffer;
const wchar_t *_strDestName=L"dest";
void RetryCancel(const wchar_t*,const wchar_t*){throw std::runtime_error("hard error");}
unsigned Copy(){
'''+block+r'''
++fallbacks;return 123;}};
int main(){for(int err:{EXDEV,EOPNOTSUPP,ENOSYS}){error_code=err;calls=0;Transfer t;
assert(t.Copy()==123 && t.Copy()==123);assert(calls==1 && t.fallbacks==2 && !t._UseCOW);}
error_code=EIO;Transfer hard;bool failed=false;try{hard.Copy();}catch(...){failed=true;}assert(failed && hard._UseCOW && !hard.fallbacks);
error_code=0;Transfer ok;assert(ok.Copy()==7 && ok._UseCOW && !ok.fallbacks);}
''')
