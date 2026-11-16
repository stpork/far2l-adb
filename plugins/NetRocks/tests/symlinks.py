from pathlib import Path
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'testing/perf-regressions'))
from common import ROOT, section, check
prefix = r'''
#include "plugins/NetRocks/src/TransferPath.h"
#include <string>
#include <map>
#include <memory>
#include <stdexcept>
#include <cstdio>
#include <cassert>
struct ProtocolUnsupportedError:std::runtime_error { using std::runtime_error::runtime_error; };
struct Host {
    std::string target, created;
    bool unsupported=false;
    void SymlinkQuery(const std::string &,std::string &s) { s=target; }
    void SymlinkCreate(const std::string &,const std::string &s) {
        if(unsupported) throw ProtocolUnsupportedError("unsupported");
        created=s;
    }
};
enum { WEK_SYMLINK_QUERY, WEK_SYMLINK_CREATE };
template<int K,class F> void WhatOnErrorWrap(int&,int&,Host*,const std::string&,F f) { f(); }
class OpXfer {
public:
    std::shared_ptr<Host> _base_host=std::make_shared<Host>(),_dst_host=std::make_shared<Host>();
    int _wea_state=0,_state=0;
    bool _smart_symlinks_copy=false;
    std::string _base_dir, _dst_dir="/d/";
    std::map<std::string,int> _entries;
    bool SymlinkCopy(const std::string &,const std::string &);
};
'''
main = r'''
int main() {
    OpXfer x;
    x._base_host->target="target"; x._dst_host->unsupported=true;
    bool result=x.SymlinkCopy("/src/link","/dst/link");
    printf("Unsupported symlink: reported_success=%d, created_target='%s'\n",result,x._dst_host->created.c_str());
    assert(!result && x._dst_host->created.empty());
    x._dst_host->unsupported=false; x._smart_symlinks_copy=true;
    x._base_dir="/very/long/source/";
    x._base_host->target=x._base_dir+"tree/data";
    x._entries[x._base_host->target]=1;
    x.SymlinkCopy(x._base_dir+"tree/link","/d/tree/link");
    printf("Smart symlink: actual='%s', expected='../tree/data' (or 'data')\n",x._dst_host->created.c_str());
    assert(x._dst_host->created=="data");
assert(RelativeTransferTarget("/d/a/b/link", "/d/c/data")=="../../c/data");
assert(RelativeTransferTarget("/d/link", "/d/file")=="file");
assert(NormalizeTransferPath("/a/../b/./file")=="/b/file");
}
'''
check('Symlink creation failure and relocated absolute targets',prefix+section('plugins/NetRocks/src/Op/OpXfer.cpp','bool OpXfer::SymlinkCopy(', 'void OpXfer::ForcefullyAbort()')+main)
