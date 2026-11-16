from pathlib import Path
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'testing/perf-regressions'))
from common import ROOT, section, check
prefix = r'''
#include <unistd.h>
#include "plugins/mtp/src/ObjectReplacement.h"
#include <algorithm>
#include <map>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <cstdio>
#include <cassert>
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
#include "plugins/mtp/src/Types.h"
static bool fail_read=false,fail_stat=false;
dirent* TestReaddir(DIR* dir){if(fail_read){errno=EIO;return nullptr;}return ::readdir(dir);}
int TestLstat(const char* path,struct stat* info){if(fail_stat){errno=EIO;return -1;}return ::lstat(path,info);}
#define readdir TestReaddir
#define lstat TestLstat
#define DBG(...) ((void)0)
std::wstring StrMB2Wide(const std::string& s){return {s.begin(),s.end()};}
std::string AsideName(const std::string& s){return s+".aside";}
int MapErrorToErrno(const proto::Status&){return EIO;}
struct ProgressTracker {
 void MarkSkipped(){} void PinNearDone(const std::string&){} void HaltBatch(){}
 bool Aborted(){return false;} void Tick(uint64_t,uint64_t,const std::string&){} void CompleteFile(uint64_t){}
};
struct WorkUnit {
 std::wstring display_name;bool is_directory=false;uint64_t total_bytes=0,total_files=0;
 std::function<int(ProgressTracker&)> execute;
};
struct Backend {
 std::map<uint32_t,proto::ObjectEntry> objects;unsigned uploads=0; bool fail_rename=false, fail_delete=false, fail_upload=false;
 proto::Status Rename(uint32_t h,const std::string& s){if(fail_rename)return proto::Status::Failure(proto::ErrorCode::Io,"rename failed");objects[h].name=s;return proto::OkStatus();}
 proto::Status Delete(uint32_t h,bool){if(fail_delete)return proto::Status::Failure(proto::ErrorCode::Io,"delete failed");objects.erase(h);return proto::OkStatus();}
 proto::Status MoveObject(uint32_t,uint32_t,uint32_t){return proto::Status::Failure(proto::ErrorCode::Unsupported,"no MoveObject");}
 proto::Result<uint32_t> CopyObject(uint32_t,uint32_t,uint32_t){return proto::Result<uint32_t>::Failure(proto::ErrorCode::Unsupported,"no CopyObject");}
 proto::Result<std::vector<proto::ObjectEntry>> ListChildren(uint32_t,uint32_t parent){std::vector<proto::ObjectEntry> v;for(auto& p:objects)if(p.second.parent==parent)v.push_back(p.second);return proto::Result<decltype(v)>::Success(v);}
 proto::Result<uint32_t> MakeDirectory(const std::string&,uint32_t,uint32_t){return proto::Result<uint32_t>::Success(42);}
 template<class F> proto::Status Upload(const std::string&,const std::string&,uint32_t,uint32_t,F,proto::CancellationToken){++uploads;if(fail_upload)return proto::Status::Failure(proto::ErrorCode::Cancelled,"cancelled");return proto::OkStatus();}
};
struct MTPPlugin {
 std::shared_ptr<Backend> _backend=std::make_shared<Backend>();
 struct RecursiveStats{uint64_t size=0,count=0;};
 struct HostFallbackItem{uint32_t src_handle;proto::ObjectEntry src_stat;uint32_t dst_storage_id,dst_parent;std::string dst_name;uint64_t total_bytes=0,total_files=0;};
 struct DeviceCopyItem{uint32_t handle;proto::ObjectEntry stat;RecursiveStats rstats;std::string target_name;bool skip=false,overwrite=false;};
 struct DeviceCopyCtx{MTPPlugin* dst;std::vector<HostFallbackItem>* fallback;proto::Status* hard_err;std::map<std::string,proto::ObjectEntry>* dst_existing;bool move;uint32_t dst_storage,dst_parent;};
 WorkUnit MakeDeviceCopyMoveUnit(const DeviceCopyItem&,const RecursiveStats&,const DeviceCopyCtx&);
 proto::Status UploadRecursive(const std::string&,const std::string&,uint32_t,uint32_t,proto::CancellationToken,proto::CancellationSource*,ProgressTracker*);
};
'''
main = r'''
int main(int argc,char** argv){
 MTPPlugin p;ProgressTracker tr;
 proto::ObjectEntry src;src.handle=1;src.name="same.txt";src.size=3;src.parent=100;
 proto::ObjectEntry dst=src;dst.handle=2;dst.parent=0;
 p._backend->objects={{1,src},{2,dst}};
 std::map<std::string,proto::ObjectEntry> existing={{dst.name,dst}};
 std::vector<MTPPlugin::HostFallbackItem> fallback;auto err=proto::OkStatus();
 MTPPlugin::DeviceCopyCtx ctx{&p,&fallback,&err,&existing,false,1,0};
 MTPPlugin::DeviceCopyItem item{1,src,{3,1},"",false,true};
 auto unit=p.MakeDeviceCopyMoveUnit(item,{3,1},ctx);int rc=unit.execute(tr);
 printf("MTP Unsupported CopyObject: rc=%d fallback=%zu original_destination_exists=%d\n",rc,fallback.size(),int(p._backend->objects.count(2)));
 assert(rc==0 && fallback.size()==1 && p._backend->objects.count(2) && p._backend->objects.at(2).name==dst.name);
 proto::CancellationSource cs;
 auto status=p.UploadRecursive(argv[1],"folder",1,0,cs.Token(),&cs,&tr);
 printf("MTP directory with regular file+symlink: success=%d uploaded_entries=%u (expected 2 or explicit rejection)\n",status.ok,p._backend->uploads);
 assert(!status.ok && status.code==proto::ErrorCode::Unsupported);
 assert(access((std::string(argv[1])+"/link").c_str(),F_OK)==0);
 assert(unlink((std::string(argv[1])+"/link").c_str())==0);
 p._backend->fail_upload=true;
 status=p.UploadRecursive(argv[1],"folder",1,0,cs.Token(),&cs,&tr);
 assert(!status.ok && status.code==proto::ErrorCode::Cancelled);
 p._backend->fail_upload=false;
 status=p.UploadRecursive(argv[1],"folder",1,0,cs.Token(),&cs,&tr);
 assert(status.ok);
 fail_read=true;
 status=p.UploadRecursive(argv[1],"folder",1,0,cs.Token(),&cs,&tr);
 assert(!status.ok && status.code==proto::ErrorCode::Io);fail_read=false;
 fail_stat=true;
 status=p.UploadRecursive(argv[1],"folder",1,0,cs.Token(),&cs,&tr);
 assert(!status.ok && status.code==proto::ErrorCode::Io);fail_stat=false;
 // Backup failure must not execute a replacement or delete the old object.
 p._backend->fail_rename=true;bool executed=false;
 auto st=ReplaceMtpObject(*p._backend,&dst,"backup",[&](){executed=true;return proto::OkStatus();});
 assert(!st.ok && !executed && p._backend->objects.at(2).name==dst.name);
 p._backend->fail_rename=false;
 st=ReplaceMtpObject(*p._backend,&dst,"backup",[](){return proto::Status::Failure(proto::ErrorCode::Io,"upload failed");});
 assert(!st.ok && p._backend->objects.at(2).name==dst.name);
 p._backend->fail_delete=true;
 st=ReplaceMtpObject(*p._backend,&dst,"backup",[](){return proto::OkStatus();});
 assert(!st.ok && p._backend->objects.at(2).name=="backup");
 p._backend->fail_delete=false;p._backend->objects.at(2)=dst;
 st=ReplaceMtpObject(*p._backend,&dst,"backup",[](){return proto::OkStatus();});
 assert(st.ok && !p._backend->objects.count(2));
}
'''

import tempfile
with tempfile.TemporaryDirectory(prefix='mtp-regression-') as tmp:
    fixture=Path(tmp)
    (fixture/'file').write_bytes(b'data')
    (fixture/'link').symlink_to('file')
    main=main.replace('argv[1]', 'argv[2]')
    unit=section('plugins/mtp/src/MTPPlugin.cpp','WorkUnit MTPPlugin::MakeDeviceCopyMoveUnit(', 'proto::Status MTPPlugin::RunHostFallbackBatch(')
    upload=section('plugins/mtp/src/MTPPlugin.cpp','proto::Status MTPPlugin::UploadRecursive(', 'proto::Status MTPPlugin::ManualCopyViaHostInline(')
    check('MTP unsupported fallback, incomplete upload and overwrite rollback', prefix+unit+upload+main, [str(fixture)])
