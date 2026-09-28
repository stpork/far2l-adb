from common import section, check
code=section('far2l/src/editor.cpp','\tif (DelPtr == TopScreen) {','\tif (DelPtr == TopList)')
check('Editor top-line deletion',r"""
#include <cassert>
struct Edit {Edit *m_next=nullptr,*m_prev=nullptr;};
struct Editor {Edit *TopScreen;int m_TopScreenVisualLine;void Cut(Edit *DelPtr){
"""+code+r"""
}};
int main(){Edit first{},second{},last{};first.m_next=&second;second.m_prev=&first;second.m_next=&last;last.m_prev=&second;
Editor e{&first,5};e.Cut(&first);assert(e.TopScreen==&second && e.m_TopScreenVisualLine==0);
e={&last,3};e.Cut(&last);assert(e.TopScreen==&second && e.m_TopScreenVisualLine==0);
e={&first,2};e.Cut(&last);assert(e.TopScreen==&first && e.m_TopScreenVisualLine==2);
}
""")
