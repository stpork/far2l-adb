from common import check, section

flush = section('far2l/src/dizlist.cpp', 'bool DizList::Flush(', 'bool DizList::Write(')
check('Concurrent description commits merge additions, removals and retries', r'''
#include <cassert>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
std::mutex dispatch;
template<class T,class F> T InterThreadCall(F action){std::lock_guard<std::mutex> lock(dispatch);return action();}
using Entries=std::map<std::wstring,std::wstring>;
Entries disk{{L"moved",L"old"},{L"kept",L"unchanged"}};
bool fail=false;
struct DizList {
 Entries entries;bool Modified=true;std::wstring strDizFileName;
 std::vector<std::wstring> ChangedNames;
 bool Read(const wchar_t*,const wchar_t*,bool){entries=disk;return true;}
 bool Write(const wchar_t*,const wchar_t*){if(fail)return false;disk=entries;return true;}
 void GetDizName(std::wstring& name){name=L"descript.ion";}
 int GetDizPosEx(const wchar_t* n,void*){return entries.count(n)?0:-1;}
 bool CopyDiz(const wchar_t* n,const wchar_t* to,DizList* dst){dst->entries[to]=entries.at(n);return true;}
 void DeleteDiz(const wchar_t* n){entries.erase(n);}
 bool Flush(const wchar_t*,const wchar_t* =nullptr);
};
''' + flush + r'''
int main(){
 DizList a,b;a.entries=b.entries=disk;
 a.entries[L"a"]=L"first";a.ChangedNames={L"a"};
 b.entries[L"b"]=L"second";b.entries.erase(L"moved");b.ChangedNames={L"b",L"moved"};
 std::thread t([&]{assert(a.Flush(L"/same"));});assert(b.Flush(L"/same"));t.join();
 assert(disk.size()==3 && disk.at(L"a")==L"first" && disk.at(L"b")==L"second" && disk.count(L"kept"));
 a.entries[L"retry"]=L"pending";a.ChangedNames={L"retry"};a.Modified=true;fail=true;
 assert(!a.Flush(L"/same") && a.Modified && !disk.count(L"retry"));
 fail=false;assert(a.Flush(L"/same") && disk.at(L"retry")==L"pending");
}
''')
