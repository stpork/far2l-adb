from pathlib import Path
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'testing/perf-regressions'))
from common import section, check

prefix = r'''
#include <cstdio>
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <cassert>
#include <cerrno>
#include <thread>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <poll.h>
#include <fcntl.h>
#include <signal.h>
#include "plugins/adb/src/ADBShell.h"
#define DBG(...) ((void)0)
static thread_local int mode=0,waits=0;
namespace ADBUtils {void TrimTrailingNewlines(std::string&){} }
int TestForkpty(int*,void*,void*,winsize*) {if(mode==0){errno=EMFILE;return -1;}return 12345;}
int TestPoll(pollfd* fd,nfds_t,int){fd->revents=POLLHUP;return 1;}
int TestWaitpid(pid_t pid,int* status,int){
    ++waits;
    if(mode==2){errno=ECHILD;return -1;}
    if(mode==3 && waits==1){errno=EINTR;return -1;}
    *status=mode==5 ? SIGTERM : 0;return pid;
}
#define forkpty TestForkpty
#define waitpid TestWaitpid
#define poll TestPoll
std::string ADBShell::findAdbExecutable(){return mode==4?"":"/mock/adb";}
std::vector<char*> PrepareEnv(std::vector<std::string>&){return {};}
std::vector<char*> PrepareArgv(const std::string&,const std::string&,const std::vector<std::string>&,std::vector<std::string>&){return {};}
'''
process = section('plugins/adb/src/ADBShell.cpp', 'std::string ADBShell::runAdbProcessWithPty(',
                  '// Instance version for device-specific ADB commands')
check('ADB launch, signals, wait failures, EINTR and parallel results', prefix + process + r'''
int main(){
 auto run=[](){waits=0;return ADBShell::adbExecWithProgress({},[](const std::string&){});};
 mode=1;run();assert(ADBShell::lastPtyExit()==0);
 mode=0;auto out=run();assert(out.empty() && ADBShell::lastPtyExit()==ADBShell::kPtyExitFailed);
 mode=5;run();assert(ADBShell::lastPtyExit()==ADBShell::kPtyExitFailed);
 mode=4;run();assert(ADBShell::lastPtyExit()==ADBShell::kPtyExitFailed);
 mode=2;run();assert(ADBShell::lastPtyExit()==ADBShell::kPtyExitFailed);
 mode=3;run();assert(waits==2 && ADBShell::lastPtyExit()==0);
 mode=0;run();assert(ADBShell::lastPtyExit()==ADBShell::kPtyExitFailed);
 std::thread other([&](){mode=1;run();assert(ADBShell::lastPtyExit()==0);});other.join();
 assert(ADBShell::lastPtyExit()==ADBShell::kPtyExitFailed);
}
''')
