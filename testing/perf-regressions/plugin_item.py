from common import section, check
from sort_fixture import base
convert=section('far2l/src/panels/flplugin.cpp','void FileList::FileListToPluginItem(', 'size_t FileList::FileListToPluginItem2(')
check('Plugin item metadata',base+convert+r"""
int main(){FileListItem item;item.strName.value=L"file";
for(unsigned mode:{0100644u,0040755u,0120777u}) {
item.FileMode=mode;item.Selected=true;item.FileSize=123;PluginPanelItem pi;memset(&pi,0xa5,sizeof(pi));
FileList::FileListToPluginItem(&item,&pi);assert(pi.FindData.dwUnixMode==mode);
assert(pi.FindData.nFileSize==123 && pi.Flags==PPIF_SELECTED && !pi.UserData && !pi.Owner && !pi.Group);
assert(!pi.Reserved[0] && !pi.Reserved[1]);assert(wcscmp(pi.FindData.lpwszFileName,L"file")==0);
FileList::FreePluginPanelItem(&pi);}}
""")
