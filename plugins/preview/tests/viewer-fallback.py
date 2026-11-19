"""Drive the production viewer event handler through initial read, focus and close."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'testing/perf-regressions'))
from common import check, section

handler = section('plugins/preview/src/Plugin.cpp', 'static std::unordered_set<int> previewed_viewers',
                  'SHAREDSYMBOL int WINAPI ConfigureW')
check('F3 fallback and viewer-specific close', r'''
#include <cassert>
#include <string>
#include <unordered_set>
#include <cwchar>
#define SHAREDSYMBOL
#define WINAPI
#define DBG(...) ((void)0)
using LONG_PTR=intptr_t; using SHORT=short;
enum {FALSE, TRUE, VE_READ, VE_CLOSE, VE_GOTFOCUS, VE_REDRAW, VCTL_GETINFO, VCTL_QUIT,
      PANEL_ACTIVE, PANEL_PASSIVE, FCTL_GETPANELINFO, PTYPE_QVIEWPANEL, PFLAGS_REALNAMES};
struct ViewerInfo {size_t size; int ViewerID; const wchar_t *FileName;};
struct SMALL_RECT {SHORT left,top,right,bottom;};
struct PanelInfo {int Visible, PanelType, Plugin, Flags; SMALL_RECT PanelRect;};
int id=7, closes=0, shows=0, g_standard_view_depth=0;
struct {bool Enabled(){return true;} bool OpenInFV(){return true;} bool OpenInQV(){return false;}
        bool MatchFile(const char*){return true;}} g_settings;
struct {void Control(int,int,int,LONG_PTR){}
        int ViewerControl(int cmd,void* p) {
            if(cmd==VCTL_QUIT){++closes;return TRUE;}
            *static_cast<ViewerInfo*>(p)={sizeof(ViewerInfo),id,L"/panel/bad.png"};return TRUE;
        }} g_far;
bool IsCurrentPanelFile(const std::string&){return false;}
std::string Wide2MB(const wchar_t* p){return {p,p+wcslen(p)};}
std::string GetCurrentPanelItem(){return {};}
bool IsShowingImageAtQV(){return false;}
void DismissImageAtQV(){}
void ShowImageAtQV(const std::string&,SMALL_RECT){}
enum EXITED_DUE{EXITED_DUE_ERROR, EXITED_DUE_ESCAPE};
EXITED_DUE result=EXITED_DUE_ERROR;
EXITED_DUE ShowImageAtFull(const std::string&,bool,bool=true){++shows;return result;}
EXITED_DUE OpenPluginAtCurrentPanel(const std::string&){++shows;return result;}
''' + handler + r'''
int main() {
    assert(!ProcessViewerEventW(VE_READ,nullptr));
    assert(!ProcessViewerEventW(VE_GOTFOCUS,&id));
    assert(!ProcessViewerEventW(VE_REDRAW,&id));
    assert(shows==1 && closes==0);
    result=EXITED_DUE_ESCAPE;
    assert(ProcessViewerEventW(VE_READ,nullptr) && closes==0);
    int other=8;
    assert(!ProcessViewerEventW(VE_GOTFOCUS,&other));
    assert(ProcessViewerEventW(VE_GOTFOCUS,&id));
    assert(!ProcessViewerEventW(VE_REDRAW,&id) && closes==1);
    assert(ProcessViewerEventW(VE_READ,nullptr));
    ProcessViewerEventW(VE_CLOSE,&id);
    assert(!ProcessViewerEventW(VE_GOTFOCUS,&id) && closes==1);
}
''')
