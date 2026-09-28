from common import section, check
from sort_fixture import base
convert=section('far2l/src/panels/flplugin.cpp','void FileList::FileListToPluginItem(', 'size_t FileList::FileListToPluginItem2(')
plugin=section('far2l/src/panels/filelist.cpp','\tif (hSortPlugin) {','\tconst wchar_t *Name1 =')
wrapper='int PluginSort(const void *el1,const void *el2){auto *SPtr1=*(FileListItem**)el1;auto *SPtr2=*(FileListItem**)el2;\n'+plugin+'return 0;\n}\n'
check('Allocation-free plugin comparison',base+convert+wrapper+r'''
int main(){std::vector<FileListItem*> items;
for(int i=0;i<10000;++i){auto *x=new FileListItem;x->strName.value=L"entry_"+std::to_wstring(100000+i);x->UserFlags=123;items.push_back(x);}
std::mt19937 rng(42);std::shuffle(items.begin(),items.end(),rng);
qsort(items.data(),items.size(),sizeof(items[0]),PluginSort);
assert(comparisons>10000 && duplicates==0 && releases==0);
for(size_t i=1;i<items.size();++i)assert(items[i-1]->strName.value<items[i]->strName.value);
for(auto *x:items){assert(x->UserFlags==123);delete x;}
}
''')
