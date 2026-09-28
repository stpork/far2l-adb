from common import section, check
state=section('WinPort/src/Backend/TTY/TTYBackend.h','\tstruct AsyncEvent','\n\tunsigned int _ae_idle_wait_request')
dispatch=section('WinPort/src/Backend/TTY/TTYBackend.cpp','void TTYBackend::DispatchOutput(','void TTYBackend::DispatchFar2lInteract(')
notify=section('WinPort/src/Backend/TTY/TTYBackend.cpp','void TTYBackend::OnConsoleOutputUpdated(','void TTYBackend::OnConsoleOutputResized(')
check('TTY dirty rows and spans',r'''
#include <vector>
#include <algorithm>
#include <cassert>
#include <mutex>
#include <condition_variable>
#include <functional>
#include "WinPort/WinCompat.h"
#include <cctweaks.h>
template<class T,class U>T CheckedCast(U v){return static_cast<T>(v);}
struct Console {int width=250,height=70;size_t cells_read=0;std::vector<CHAR_INFO> cells;std::function<void()> during_read;
Console():cells(width*height){for(auto &c:cells)c.Char.UnicodeChar=L'A';}
void Read(CHAR_INFO *out,COORD size,COORD pos,SMALL_RECT rect){for(int y=rect.Top;y<=rect.Bottom;++y)for(int x=rect.Left;x<=rect.Right;++x){
out[(pos.Y+y-rect.Top)*size.X+pos.X+x-rect.Left]=cells[y*width+x];++cells_read;}
if(during_read){auto f=during_read;during_read=nullptr;f();}}
COORD GetCursor(UCHAR &height,bool &visible){height=1;visible=true;return {0,0};}} console;
auto *g_winport_con_out=&console;
struct TTYOutput {unsigned width=250,x=0,y=0,writes=0;std::vector<CHAR_INFO> display=std::vector<CHAR_INFO>(250*70);
void MoveCursorLazy(unsigned row,unsigned col){y=row-1;x=col-1;}
int WeightOfHorizontalMoveCursor(unsigned,unsigned){return -1;}
void WriteLine(const CHAR_INFO *p,unsigned n){++writes;for(unsigned i=0;i<n;++i)display[y*width+x++]=p[i];}
void ChangeCursor(bool){}void ChangeCursorHeight(unsigned){}};
struct TTYBackend {
unsigned _cur_width=250,_cur_height=70,_prev_width=0,_prev_height=0;int _far2l_cursor_height=-1;
std::vector<CHAR_INFO> _cur_output,_prev_output;std::mutex _async_mutex;std::condition_variable _async_cond;
'''+state+r'''
void DispatchOutput(TTYOutput&,const AsyncEvent&);void OnConsoleOutputUpdated(const SMALL_RECT*,size_t);
void draw(TTYOutput &o){AsyncEvent a{};{std::lock_guard<std::mutex> l(_async_mutex);std::swap(a,_ae);}DispatchOutput(o,a);}};
'''+dispatch+notify+r'''
void equal(const TTYOutput&o){assert(o.display.size()==console.cells.size());for(size_t i=0;i<o.display.size();++i){assert(o.display[i].Char.UnicodeChar==console.cells[i].Char.UnicodeChar);assert(o.display[i].Attributes==console.cells[i].Attributes);}}
int main(){TTYBackend b;TTYOutput out;b.OnConsoleOutputUpdated(nullptr,0);b.draw(out);equal(out);assert(console.cells_read==17500);
console.cells_read=0;out.writes=0;SMALL_RECT r{10,3,11,3};console.cells[3*250+10].Char.UnicodeChar=L'B';console.cells[3*250+11].Char.UnicodeChar=L'C';
b.OnConsoleOutputUpdated(&r,1);b.draw(out);equal(out);assert(console.cells_read==250 && out.writes==1);
console.cells_read=0;b._ae.output=true;b.draw(out);assert(console.cells_read==0);equal(out);
// A notification arriving during Read belongs to the next batch.
console.during_read=[&]{console.cells[10*250].Char.UnicodeChar=L'Z';SMALL_RECT next{0,10,0,10};b.OnConsoleOutputUpdated(&next,1);};
b.OnConsoleOutputUpdated(&r,1);b.draw(out);assert(b._ae.output);b.draw(out);equal(out);
SMALL_RECT clipped{-10,-10,300,100};b.OnConsoleOutputUpdated(&clipped,1);b.draw(out);equal(out);
console.width=4;console.height=4;console.cells.assign(16,CHAR_INFO{});b._cur_width=4;b._cur_height=4;out.width=4;out.display.resize(16);b.draw(out);equal(out);
b._cur_width=b._cur_height=0;b.draw(out);
}
''')
