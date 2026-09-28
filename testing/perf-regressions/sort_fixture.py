from common import section
prefix=r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
#include <random>
#include "WinPort/WinCompat.h"
// Only the string facade and outer application are mocked. The item/API layouts
// and conversion/comparison statements below are extracted verbatim.
struct FARString {
    std::wstring value;
    size_t GetLength() const {return value.size();}
    wchar_t At(size_t n) const {return value.at(n);}
    bool IsEmpty() const {return value.empty();}
    const wchar_t* CPtr() const {return value.c_str();}
    operator const wchar_t*() const {return CPtr();}
};
struct HighlightDataColor {}; const HighlightDataColor ZeroColors{};
enum {PPIF_SELECTED=0x40000000,PPIF_USERDATA=0x20000000,UNSORTED=0,SM_UNSORTED=0};
int ListSortMode=1,ListSortOrder=1; bool ListSelectedFirst=false;
size_t duplicates=0,releases=0,comparisons=0;
wchar_t *audit_wcsdup(const wchar_t *s) {++duplicates;return wcsdup(s);}
#define FAR_USE_INTERNALS
'''
item=section('far2l/src/panels/filelist.hpp','struct FileListItem\n','template <class T>')
api=section('far2l/far2sdk/farplug-wide.h','struct FAR_FIND_DATA\n','enum PANELINFOFLAGS')
glue=r'''
FileListItem::FileListItem()=default;FileListItem::~FileListItem()=default;
void apiFreeFindData(FAR_FIND_DATA *p) {++releases;free(p->lpwszFileName);}
struct FileList {static void FileListToPluginItem(FileListItem*,PluginPanelItem*);static void FreePluginPanelItem(PluginPanelItem*);};
struct PluginStub {int Compare(void*,const PluginPanelItem *a,const PluginPanelItem *b,int) {
    ++comparisons;int r=wcscmp(a->FindData.lpwszFileName,b->FindData.lpwszFileName);return (r>0)-(r<0);
}};
struct {PluginStub Plugins;} ctrl;
auto *CtrlObject=&ctrl;void *hSortPlugin=&ctrl;
#define wcsdup audit_wcsdup
'''
base=prefix+item+api+glue
