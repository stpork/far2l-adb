from common import section, check
from copy_fixture import prefix
source=section('far2l/src/copy.cpp','static std::pair<DWORD, DWORD> LookupNextHole(', '/////////////////////////////////////////////////////////// END OF ShellFileTransfer')
check('Copy short-write accounting',prefix+source+r'''
int main(){for(size_t limit:{size_t(3000),size_t(6000),size_t(7168),size_t(8192)}) {
ShellFileTransfer x;x._SrcFile.bytes.assign(6000,'x');x._DstFlags=FILE_FLAG_NO_BUFFERING;x._DestFile.write_limit=limit;
size_t total=0;for(int i=0;i<5;++i){DWORD n=x.PieceCopy();total+=n;if(!n)break;}
assert(total==6000 && x._DestFile.bytes==x._SrcFile.bytes);
}
ShellFileTransfer y;y._SrcFile.bytes.assign(6000,'y');y._DestFile.write_limit=3000;
assert(y.PieceCopy()==3000);assert(y.PieceCopy()==3000);assert(y._SrcFile.bytes==y._DestFile.bytes);
}
''')
