from common import ROOT, check
source=(ROOT/'WinPort/src/APIConsole.cpp').read_text()
source=source[source.index('\tstatic struct {\n\t\tstruct Cmp'):]
check('Bounded composite registry with stable IDs',r'''
#include <mutex>
#include <map>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cassert>
#include "WinPort/WinCompat.h"
#define WINPORT_DECL(n,t,a) t n a
extern "C" {
'''+source+r'''
int main(){
assert(CompositeCharRegister(L"")==0);assert(CompositeCharRegister(L"x")==L'x');
const wchar_t *first=nullptr;COMP_CHAR first_id=0;
for(size_t i=0;i<s_composite_chars.MaxEntries;++i){std::wstring seq=L"a"+std::to_wstring(i)+L"\u0301";
auto id=CompositeCharRegister(seq.c_str());assert(id&COMPOSITE_CHAR_MARK);
if(!i){first_id=id;first=CompositeCharLookup(id);}assert(wcscmp(CompositeCharLookup(id),seq.c_str())==0);}
assert(s_composite_chars.id2str.size()==s_composite_chars.MaxEntries);
auto bytes=s_composite_chars.payload_bytes;
assert(CompositeCharRegister(L"z\u0301")==L'z');assert(s_composite_chars.payload_bytes==bytes);
assert(CompositeCharRegister(L"a0\u0301")==first_id && CompositeCharLookup(first_id)==first);
assert(wcscmp(first,L"a0\u0301")==0);
std::wstring huge(1025,L'a');assert(CompositeCharRegister(huge.c_str())==L'a');
// Exercise the byte cap independently, without changing production limits.
for(auto *p:s_composite_chars.id2str)free(p);
s_composite_chars.id2str.clear();s_composite_chars.str2id.clear();s_composite_chars.payload_bytes=0;
for(size_t i=0;i<10000;++i){std::wstring seq=L"b"+std::to_wstring(i);seq.resize(1024,L'\u0301');CompositeCharRegister(seq.c_str());}
assert(s_composite_chars.payload_bytes<=s_composite_chars.MaxPayloadBytes);
assert(s_composite_chars.id2str.size()<s_composite_chars.MaxEntries);
std::wstring last(1024,L'\u0301');last[0]=L'c';bytes=s_composite_chars.payload_bytes;
assert(CompositeCharRegister(last.c_str())==L'c' && s_composite_chars.payload_bytes==bytes);
for(auto *p:s_composite_chars.id2str)free(p);
}
''')
