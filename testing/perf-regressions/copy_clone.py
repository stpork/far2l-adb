from common import section, check
block=section('far2l/src/copy.cpp','#if defined(COW_SUPPORTED) && defined(__APPLE__)','\n\t\tShellFileTransfer(SrcName,')
check('Clone overwrite, append and resume fallback',r'''
#include <cassert>
#include <string>
#include <cerrno>
#include <cstdio>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/clonefile.h>
#endif
int calls=0,inject=0;
int audit_clone(const char *a,const char *b,int flags){++calls;if(inject){errno=inject;return -1;}
#ifdef __APPLE__
return clonefile(a,b,flags);
#else
errno=ENOTSUP;return -1;
#endif
}
#define __APPLE__ 1
#define COW_SUPPORTED 1
#define clonefile audit_clone
struct ErrnoSaver {int e=errno;int Get(){return e;}};
struct Name:std::string {using std::string::string;const std::string &GetMB()const{return *this;}};
std::string Wide2MB(const char *s){return s;}
struct Progress {bool Cancelled(){return false;}} progress;auto *CP=&progress;
enum{COPY_SUCCESS=1,COPY_CANCEL=2};
struct Data {unsigned nFileSize=4;};
void ProgressUpdate(bool,const Data&,const Name&){}
int copy(const char *SrcName,const Name &strDestName,bool Append,bool Resume){
struct {bool USECOW=true;} Flags;Data SrcData;
unsigned CurCopiedSize=0,TotalCopiedSize=0;bool ShowTotalCopySize=false;
try {
'''+block+r'''
return 42;}catch(ErrnoSaver &e){return -e.Get();}}
int main(int argc,char **argv){assert(argc==2 && chdir(argv[1])==0);
FILE *f=fopen("src","w");assert(f);fputs("data",f);fclose(f);
f=fopen("dst","w");assert(f);fputs("old",f);fclose(f);
assert(copy("src",Name("dst"),false,false)==42);
int before=calls;assert(copy("src",Name("dst"),true,false)==42 && calls==before);
assert(copy("src",Name("dst"),false,true)==42 && calls==before);
int result=copy("src",Name("new"),false,false);assert(result==1 || result==42);
inject=EPERM;assert(copy("src",Name("blocked"),false,false)==-EPERM);
f=fopen("dst","r");assert(f);char text[4]={};assert(fread(text,1,3,f)==3);assert(std::string(text)=="old");fclose(f);
}
''')
