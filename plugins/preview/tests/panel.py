"""Exercise production panel lookup, selection propagation and navigation."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'testing/perf-regressions'))
from common import check, section

prefix = r'''
#include <string>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <sys/types.h>
#define DBG(...) ((void)0)
using LONG_PTR=intptr_t; using BOOL=int;
constexpr int TRUE=1,FALSE=0,PANEL_ACTIVE=1,PFLAGS_REALNAMES=1;
enum{FCTL_GETPANELINFO,FCTL_GETPANELITEM,FCTL_BEGINSELECTION,FCTL_SETSELECTION,FCTL_ENDSELECTION,FCTL_REDRAWPANEL};
enum EXITED_DUE{EXITED_DUE_ERROR,EXITED_DUE_ESCAPE,EXITED_DUE_ENTER};
struct PanelInfo{int ItemsNumber=2,Flags=PFLAGS_REALNAMES,CurrentItem=0,TopPanelItem=0;};
struct PanelRedrawInfo{int CurrentItem,TopPanelItem;};
std::vector<bool> applied(2,false);
int updates=0;
struct Far{void Control(int,int cmd,size_t index,LONG_PTR p){
 if(cmd==FCTL_GETPANELINFO)*reinterpret_cast<PanelInfo*>(p)=PanelInfo{};
 if(cmd==FCTL_SETSELECTION){applied.at(index)=p!=0;++updates;}
}} g_far;
struct Settings{bool MatchFile(const char*){return true;}}g_settings;
std::pair<std::string,bool> GetPanelItem(int,int i){return {i==0?"a.png":"b.png",false};}
std::string GetCurrentPanelItem(){return "a.png";}
EXITED_DUE result=EXITED_DUE_ESCAPE;
EXITED_DUE ShowImageAtFull(size_t,std::vector<std::pair<std::string,bool>>&,std::unordered_set<std::string>& selection,bool,size_t*){
 selection.insert("b.png");return result;
}
'''
panel = section('plugins/preview/src/Plugin.cpp', 'static ssize_t GetPanelItemsForView(',
                'static EXITED_DUE OpenPluginAtSomePath(')
main = r'''
int main(){
 std::vector<std::pair<std::string,bool>> files;
 auto index=GetPanelItemsForView("b.png",files);
 assert(index==1 && files.at(index).first=="b.png");
 files.clear();assert(GetPanelItemsForView("missing.png",files)==-1);
 assert(OpenPluginAtCurrentPanel("a.png")==EXITED_DUE_ESCAPE);
 assert(updates==2 && !applied[0] && applied[1]);
 result=EXITED_DUE_ERROR;updates=0;
 assert(OpenPluginAtCurrentPanel("a.png")==EXITED_DUE_ERROR && updates==0);
}
'''
check('Preview requested filename and panel selection on exit', prefix + panel + main)

prefix = r'''
#include <vector>
#include <string>
#include <cassert>
struct ImageView{
 std::vector<std::pair<std::string,bool>> _all_files={{"a.png",false},{"b.png",false}};
 size_t _cur_file=0;
 void DenoteState(){}
 bool Iterate(bool forward){_cur_file=forward?1:0;return true;}
 void Select();void Deselect();void ToggleSelection();
};
'''
view = (ROOT / 'plugins/preview/src/ImageView.cpp').read_text()
selection = view[view.index('void ImageView::Select()'):]
main = r'''
int main(){
 ImageView v;v.Select();assert(v._all_files[0].second && v._cur_file==1);
 v._all_files[1].second=true;v.Deselect();assert(!v._all_files[1].second && v._cur_file==0);
 v.Select();assert(v._cur_file==1); // already marked files still advance
 v.Deselect();assert(v._cur_file==0); // unmarked files still step back
}
'''
check('Preview select/deselect navigation', prefix + selection + main)
