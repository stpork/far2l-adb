from common import section, check
from sort_fixture import base
prefix=section('far2l/src/panels/filelist.cpp','int _cdecl SortList(const void *el1, const void *el2)\n{','\tif (ListDirectoriesFirst)')
check('Sort comparator equality and ordering',base+prefix+'return 0;\n}\n'+r'''
int main(){FileListItem objects[6];FileListItem *p[6];
for(int i=0;i<6;++i){p[i]=&objects[i];p[i]->strName.value=i<2?L"..":L"name";p[i]->Position=i/2;p[i]->Selected=(i%2);}
ListSortMode=UNSORTED;
for(int order:{-1,1})for(bool selected:{false,true}) {ListSortOrder=order;ListSelectedFirst=selected;
for(auto a:p)for(auto b:p){int ab=SortList(&a,&b),ba=SortList(&b,&a);assert(ab==-ba);assert(SortList(&a,&a)==0);
for(auto c:p)if(ab<0 && SortList(&b,&c)<0)assert(SortList(&a,&c)<0);}}
}
''')
