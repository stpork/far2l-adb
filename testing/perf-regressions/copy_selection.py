from common import section, check

methods = section('far2l/src/copy.cpp', 'void ShellCopy::CompleteSelection(', 'void ShellCopy::PerformCopy(')
check('Source marks after cancellation, partial completion and panel navigation', r'''
#include <cassert>
#include <string>
#include <set>
#include <unordered_set>
struct FARString:std::wstring {using std::wstring::wstring;using std::wstring::operator=;
 const wchar_t* CPtr()const{return c_str();}};
using DWORD=unsigned;
const wchar_t* PointToName(const FARString& n){return n.c_str()+n.rfind(L'/')+1;}
constexpr int NORMAL_PANEL=0;
struct Panel {
 FARString dir=L"/src";std::set<std::wstring> marked={L"a",L"b",L"c"}; int index=0;
 bool GetSelNameCompat(FARString* name,DWORD&){if(!name){index=0;return true;}
    while(index<3){*name=std::wstring(1,L'a'+index++);if(marked.count(*name))return true;}return false;}
 void Redraw(){} void GetCurDir(FARString& out){out=dir;} int GetMode(){return NORMAL_PANEL;}
 void ClearLastGetSelection(){marked.erase(std::wstring(1,L'a'+index-1));}
} panel,other;
struct Panels{Panel *LeftPanel=&panel,*RightPanel=nullptr;} panels;
struct Ctrl{Panels* Cp(){return &panels;}} ctrl,*CtrlObject=&ctrl;
bool CmpFullNames(const FARString&a,const FARString&b){return a==b;}
struct ShellCopy{struct{bool CURRENTONLY=false,COPYLASTTIME=true;} Flags;
 bool Background=true,TrackSelection=true;Panel* SrcPanel=&panel;FARString SourceDir=L"/src";
 std::unordered_set<std::wstring> CompletedSelection;
 void CompleteSelection(const FARString&);void ApplyCompletedSelection();};
''' + methods + r'''
int main(){
 ShellCopy op; op.ApplyCompletedSelection();assert(panel.marked.size()==3);
 op.Flags.COPYLASTTIME=false;op.CompleteSelection(L"/src/a");assert(op.CompletedSelection.empty());
 op.Flags.COPYLASTTIME=true;op.CompleteSelection(L"/src/a");
 panel.dir=L"/other";op.ApplyCompletedSelection();assert(panel.marked.size()==3);
 panel.dir=L"/src";panels.LeftPanel=&other;op.ApplyCompletedSelection();assert(panel.marked.size()==3);
 panels.LeftPanel=&panel;op.ApplyCompletedSelection();
 assert(!panel.marked.count(L"a") && panel.marked.count(L"b") && panel.marked.count(L"c"));
}
''')
