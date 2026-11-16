from pathlib import Path
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'testing/perf-regressions'))
from common import section, check

prefix = r'''
#include <string>
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <sys/wait.h>
#include "plugins/adb/src/ObjectReplacement.h"
namespace fs=std::filesystem;
namespace ADBUtils {
int CheckConnection(bool){return 0;}
std::string ShellQuote(const std::string& text){
    std::string q="'";for(char c:text)q+=c=='\''?"'\\''":std::string(1,c);return q+"'";
}
}
struct ADBDevice {
    bool _connected=true;
    int exit_code=0;
    void EnsureConnection(){}
    int LastShellExitCode(){return exit_code;}
    std::string RunShellCommand(const std::string& command){int status=system(command.c_str());exit_code=WIFEXITED(status)?WEXITSTATUS(status):-1;return "";}
    int SaveRemoteBackup(const std::string&,const std::string&);
    int DeleteDirectory(const std::string& path){RunShellCommand("rm -rf -- "+ADBUtils::ShellQuote(path));return exit_code?EIO:0;}
    int RestoreRemoteBackup(const std::string&,const std::string&,bool);
};
int MutationResultToErrno(int status,const std::string&){return status?EIO:0;}
'''
restore = section('plugins/adb/src/ADBDevice.cpp', 'int ADBDevice::SaveRemoteBackup(',
                  'bool ADBDevice::FileExists(')
check('ADB replacement protects existing files and restores partial directories', prefix + restore + r'''
void put(const std::string& path,const char* data){std::ofstream(path)<<data;}
std::string get(const std::string& path){std::ifstream in(path);return {std::istreambuf_iterator<char>(in),{}};}
int main(int argc,char** argv){
    auto dst=std::string(argv[1])+"/dest 'quoted' $(literal)", aside=dst+".backup";
    ADBDevice device;put(dst,"old");bool executed=false;
    fs::create_directory(aside);put(aside+"/stale","previous backup");
    int result=ReplaceAdbObject(device,dst,aside,true,[&](){executed=true;return EIO;});
    assert(get(aside+"/stale")=="previous backup");fs::remove_all(aside);
    assert(result==EIO && !executed && get(dst)=="old");
    bool retry=false;
    result=ReplaceAdbObject(device,dst,aside,true,[&](){put(dst,"partial");return EIO;},&retry);
    assert(result && retry && get(dst)=="old" && !fs::exists(aside));
    fs::remove(dst);fs::create_directory(dst);put(dst+"/old-file","old directory");
    result=ReplaceAdbObject(device,dst,aside,true,[&](){fs::create_directory(dst);put(dst+"/partial","incomplete");return ECANCELED;});
    assert(result==ECANCELED && get(dst+"/old-file")=="old directory" && !fs::exists(dst+"/partial"));
    // A failed move may contain the only remaining source data. Preserve both
    // partial destination and backup; never put the backup inside the directory.
    retry=true;
    result=ReplaceAdbObject(device,dst,aside,true,[&](){fs::create_directory(dst);put(dst+"/moved","source data");return EIO;},&retry,true);
    assert(result && !retry && get(dst+"/moved")=="source data" && get(aside+"/old-file")=="old directory");
    assert(!fs::exists(dst+"/"+fs::path(aside).filename().string()));
    fs::remove_all(dst);fs::rename(aside,dst);
    result=ReplaceAdbObject(device,dst,aside,true,[&](){fs::create_directory(dst);put(dst+"/new","complete");return 0;});
    assert(!result && get(dst+"/new")=="complete" && !fs::exists(aside));
    // Exceptions preserve the old destination as well.
    result=0;try{ReplaceAdbObject(device,dst,aside,true,[&]()->int{throw 1;});}catch(int){result=1;}
    assert(result && get(dst+"/new")=="complete" && !fs::exists(aside));
}
''')
