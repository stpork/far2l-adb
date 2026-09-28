from common import section, check
check('Full path normalization',r'''
#include <string>
#include <vector>
#include <cwchar>
#include <cstring>
#include <cerrno>
#include <cassert>
using DWORD=unsigned;using WCHAR=wchar_t;using LPCTSTR=const wchar_t*;using LPTSTR=wchar_t*;
#define WINPORT(n) n
#define WINPORT_DECL(n,t,a) t n a
#define MAX_PATH 16
#define GOOD_SLASH L'/'
std::wstring cwd=L"/home/user";bool fail=false;
DWORD GetCurrentDirectory(DWORD n,wchar_t *p){if(fail)return 0;if(n<=cwd.size())return cwd.size()+1;wcscpy(p,cwd.c_str());return cwd.size();}
'''+section('WinPort/src/APIFiles.cpp','\tWINPORT_DECL(GetFullPathName,','\n\n}')+r'''
void test(const wchar_t *in,const wchar_t *expected){
    wchar_t out[2048];wchar_t *part=nullptr;DWORD len=wcslen(expected);
    assert(GetFullPathName(in,0,nullptr,nullptr)==len+1);
    out[0]=L'!';assert(GetFullPathName(in,len,out,&part)==len+1 && out[0]==L'!' && !part);
    assert(GetFullPathName(in,2048,out,&part)==len);assert(wcscmp(out,expected)==0);
    assert(part==wcsrchr(out,L'/')+1);
}
int main(){test(L".git",L"/home/user/.git");test(L"../x",L"/home/x");test(L".",L"/home/user");
test(L"./x",L"/home/user/x");test(L"/../../a//b/../.config/",L"/a/.config/");test(L"/",L"/");
cwd=L"/";test(L"x",L"/x");cwd=std::wstring(1000,L'a');cwd[0]=L'/';test(L"x",(cwd+L"/x").c_str());
fail=true;wchar_t out[10];assert(!GetFullPathName(L"relative",10,out,nullptr));
assert(!GetFullPathName(nullptr,0,nullptr,nullptr));assert(!GetFullPathName(L"",0,nullptr,nullptr));
}
''')
