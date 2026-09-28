from common import section, check
prefix=r'''
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
using INT64=int64_t;using UINT64=uint64_t;using DWORD=uint32_t;using LPBYTE=unsigned char*;using LPVOID=void*;
template<class T,class U> T CheckedCast(U v){return static_cast<T>(v);}
template<class T,class U> T AlignDown(T n,U a){return n&~T(a-1);}
template<class T,class U> T AlignUp(T n,U a){return AlignDown(n+a-1,a);}
bool pseudo=false;
int audit_fstat(int fd,struct stat* s){int rc=fstat(fd,s);if(!rc && pseudo)s->st_size=0;return rc;}
#define sdc_fstat audit_fstat
#define sdc_open open
#define sdc_close close
#define sdc_read read
#define sdc_pread pread
void HintFDSequentialAccess(int){}
#define PSEUDOFILE_FULLREAD_LIMIT 0x1000000
#define PSEUDOFILE_FULLREAD_BLOCK 0x1000

#include <cerrno>
int live=0;bool oom=false;
void *audit_malloc(size_t n){if(oom)return nullptr;void *p=malloc(n);if(p)++live;return p;}
int audit_align(void **p,size_t a,size_t n){if(oom)return ENOMEM;int r=posix_memalign(p,a,n);if(!r)++live;return r;}
void audit_free(void *p){if(p)--live;free(p);}
#define malloc audit_malloc
#define posix_memalign audit_align
#define free audit_free
'''
source=section('far2l/src/cache.hpp','class BufferedFileView','class CachedWrite')
source+=section('far2l/src/cache.cpp','BufferedFileView::BufferedFileView()', '/////////////')
main=r'''
int main(int argc,char **argv){
assert(argc==2 && chdir(argv[1])==0);
int fd=open("small",O_CREAT|O_RDWR,0600);assert(fd>=0);assert(write(fd,"data",4)==4);close(fd);
fd=open("large",O_CREAT|O_RDWR,0600);assert(fd>=0);assert(ftruncate(fd,17*1024*1024)==0);close(fd);
{BufferedFileView v;
for(int i=0;i<5;++i){pseudo=true;assert(v.Open("small"));assert(live==1);v.SetPointer(100);
pseudo=false;assert(v.Open("large"));UINT64 n=0;v.GetSize(n);assert(n==17*1024*1024);INT64 ptr;v.GetPointer(ptr);assert(!ptr);
DWORD req=2;assert(v.ViewBytesAt(0,req));assert(live==1);}
v.Close();assert(!live);pseudo=true;oom=true;assert(!v.Open("small"));assert(!v.Opened() && !live);oom=false;
pseudo=false;assert(v.Open("large"));assert(!v.Open("missing"));assert(!v.Opened());
}
assert(!live);
}
'''
check('Viewer reopen and allocation lifetime',prefix+source+main)
