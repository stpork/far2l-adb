from common import section, check
block=section('far2l/src/copy.cpp','\t_DstFlags = FILE_FLAG_SEQUENTIAL_SCAN;','\n\tbool DstOpened =')
check('Buffered append and resume',r'''
#include <cassert>
#include <initializer_list>
#define __linux__ 1
#define USE_PAGE_SIZE 4096
enum{FILE_FLAG_SEQUENTIAL_SCAN=1,FILE_FLAG_WRITE_THROUGH=2,FILE_FLAG_NO_BUFFERING=4};
int flags(bool Append,bool Resume,bool through,unsigned size){
int _DstFlags;struct {bool WRITETHROUGH;} Flags{through};struct {unsigned nFileSize;} SrcData{size};
'''+block+r'''
return _DstFlags;}
int main(){for(bool a:{false,true})for(bool r:{false,true})for(bool w:{false,true}) {
int f=flags(a,r,w,1024*1024);assert(bool(f&FILE_FLAG_NO_BUFFERING)==(!a && !r && w));
assert(bool(f&FILE_FLAG_WRITE_THROUGH)==w);
assert(!(flags(a,r,w,1003)&FILE_FLAG_NO_BUFFERING));}}
''')
