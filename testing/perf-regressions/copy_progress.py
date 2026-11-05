"""Compile production copy progress methods with deterministic counters and ASan/UBSan."""
from common import section, check
setup = section('far2l/src/copy.cpp', '\t// нужно ли показывать время копирования?', '\n\tif (Background) {')
snapshot = section('far2l/src/copy.cpp', 'struct CopyProgressSnapshot\n', '\nclass CopyProgress final')
progress_class = section('far2l/src/copy.cpp', 'class CopyProgress final', '\nstatic void GetTimeText(')
constructor = section('far2l/src/copy.cpp', 'CopyProgress::CopyProgress(', '\nvoid CopyProgress::SetScanName(')
time_text = section('far2l/src/copy.cpp', 'static void GetTimeText(', '\nbool CopyProgress::Timer(')
progress = section('far2l/src/copy.cpp', 'void CopyProgress::SetProgress(', '\nvoid CopyProgress::SetFileCounts(')
flush = section('far2l/src/copy.cpp', 'void CopyProgress::Flush()', '\nCopyProgress::CopyProgress(')

check('Multi-destination and resume progress/ETA (foreground and background)', r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>
using DWORD = uint32_t; using UINT64 = uint64_t; using SHORT = short;
using std::min;
#define Min min
DWORD now = 1000;
DWORD GetProcessUptimeMSec() { return now; }
long WaitUserTime = 0;
uint64_t speed = 0;
int titlePercent = -1;
std::wstring timeInfo;
struct FARString {
    std::wstring s;
    void Format(const wchar_t *format, ...) {
        wchar_t buffer[256]; va_list args; va_start(args, format);
        vswprintf(buffer, 256, format, args); va_end(args); s = buffer;
        if (std::wstring(format) == L"%ls|%ls|%ls") timeInfo = s;
    }
    wchar_t At(size_t n) const { return s.at(n); }
    size_t GetLength() const { return s.size(); }
    const wchar_t *CPtr() const { return s.c_str(); }
    void LShift(size_t n) { s.erase(0, n); }
    void operator+=(const wchar_t *text) { s += text; }
};
struct ShellCopyProgressState {
    uint64_t CurResumeOffset = 0, TotalResumeOffset = 0, TotalSkippedSize = 0;
    bool ShowTotalCopySize = true; int TotalFilesToProcess = 0;
    uint64_t TotalCopiedSize = 0, TotalCopySize = 0;
    DWORD CopyStartTime = 1000; long OldCalcTime = 0;
};
struct ConsoleTitle { void Set(const wchar_t*, int percent, const wchar_t*) { titlePercent=percent; } };
struct wakeful {};
struct Manager { int operator[](int) { return 0; } } manager;
Manager *FrameManager=&manager;
struct LockFrame { explicit LockFrame(int) {} };
bool CheckForEscSilent() { return false; }
bool ConfirmAbortOp() { return false; }
struct SMALL_RECT { SHORT Left=0, Top=0, Right=79, Bottom=20; };
struct COORD { SHORT X,Y; };
struct DirInfoProgressTracker {
    virtual ~DirInfoProgressTracker() = default;
    virtual void OnDirInfoProgress(const wchar_t*) = 0;
    virtual bool IsDirInfoCancelled() const = 0;
    virtual bool AllowDirInfoUserBreak() const = 0;
};
constexpr size_t CopyProgressContentWidth = 54;
constexpr int COL_DIALOGTEXT = 0, COLUMN_FLOATSIZE = 1, COLUMN_COMMAS = 2;
uint64_t FarColorToReal(int) { return 0; }
namespace Msg { const wchar_t *CopyTimeInfo = L"%ls|%ls|%ls"; FARString CopyMovingTitle,CopyCopyingTitle; }
namespace fmt { struct Expand { explicit Expand(int) {} }; }
struct FormatString { template<class T> FormatString &operator<<(const T&) { return *this; } };
constexpr unsigned BS_X_B0=0, BS_X_DB=1;
wchar_t BoxSymbols[] = {L'.', L'#'};
int ToPercent64(uint64_t n, uint64_t d) { return d ? n*100/d : 0; }
template<class T> void Text(int,int,uint64_t,const T&) {}
void FileSizeToStr(FARString &text, uint64_t n, int, int) { speed=n; text.s=std::to_wstring(n); }
constexpr unsigned COPY_RULE_FILES = 1;
struct { struct { unsigned CopyTimeRule = COPY_RULE_FILES; } CMOpt; } Opt;
void PrepareCopy(ShellCopyProgressState &ProgressState, int count, bool folder, size_t destinations=1)
{
    struct { int SelCount; bool FolderPresent; } CDP{count, folder};
    std::vector<int> OperationDestinations(destinations);
''' + setup + r'''
}
''' + snapshot + progress_class + time_text + constructor + r'''
bool CopyProgress::Timer() { return true; }
''' + flush + r'''
void CopyProgress::CreateBackground() { BgInit = true; }
void CopyProgress::SetScanName(const wchar_t*) {}
''' + progress + r'''
int main(int argc, char **argv)
{
    const uint64_t M = 1024*1024;
    for (bool bg : {true, false}) {
        now=1000;
        ShellCopyProgressState state;
        PrepareCopy(state,1,false);
        CopyProgress p(state,false,state.ShowTotalCopySize,true,bg);
        state.CurResumeOffset=64*M;
        now=5000;
        p.SetProgressValue(16*M,96*M);
        if (bg) {
            CopyProgressSnapshot out; p.GetSnapshot(out);
            assert(!out.showTotal && out.currentCompleted==80*M && out.currentTotal==96*M);
            assert(out.bytesPerSecond==4*M && out.remainingSeconds==4);
            // Reopening uses the same counters; only newly transferred bytes affect speed.
            now=7000; p.SetProgressValue(24*M,96*M); p.GetSnapshot(out);
            assert(out.bytesPerSecond==4*M && out.remainingSeconds==2);
            p.SetProgressValue(32*M,96*M); p.GetSnapshot(out);
            assert(out.currentCompleted==96*M && out.remainingSeconds==0);
        } else {
            assert(speed==4*M && timeInfo==L"00:00:04|00:00:04|4194304");
            assert(titlePercent==83);
        }
    }
    for (bool bg : {true, false}) {
        now=1000;
        ShellCopyProgressState state;
        PrepareCopy(state,1,false,2);
        CopyProgress p(state,false,state.ShowTotalCopySize,true,bg);
        state.TotalResumeOffset=96*M; state.TotalSkippedSize=32*M;
        state.TotalCopiedSize=64*M; state.TotalCopySize=192*M;
        now=5000;
        p.SetTotalProgressValue(64*M,192*M);
        if (bg) {
            CopyProgressSnapshot out; p.GetSnapshot(out);
            assert(out.showTotal && out.overallCompleted==160*M && out.overallTotal==192*M);
            assert(out.bytesPerSecond==8*M && out.remainingSeconds==4);
        } else {
            assert(speed==8*M && timeInfo==L"00:00:04|00:00:04|8388608");
            assert(titlePercent==83);
        }
    }
    for (bool folder : {false,true}) {
        ShellCopyProgressState state;
        PrepareCopy(state,folder?1:2,folder);
        assert(state.ShowTotalCopySize);
    }
    now=1000; ShellCopyProgressState normal;
    PrepareCopy(normal,1,false);
    CopyProgress n(normal,false,normal.ShowTotalCopySize,true,true);
    now=5000; n.SetProgressValue(16*M,32*M);
    CopyProgressSnapshot normalSnapshot; n.GetSnapshot(normalSnapshot);
    assert(!normalSnapshot.showTotal && normalSnapshot.currentCompleted==16*M);
    assert(normalSnapshot.bytesPerSecond==4*M && normalSnapshot.remainingSeconds==4);
    // The total-progress preference is preserved for multiple destinations.
    ShellCopyProgressState disabled; disabled.ShowTotalCopySize=false;
    PrepareCopy(disabled,1,false,2); assert(!disabled.ShowTotalCopySize);
    // Over-completion or oversized prefixes must not underflow the remaining size.
    now=1000; ShellCopyProgressState state;
    CopyProgress p(state,false,false,true,true);
    state.CurResumeOffset=UINT64_MAX;
    now=5000; p.SetProgressValue(16,8);
    CopyProgressSnapshot out; p.GetSnapshot(out);
    assert(out.currentCompleted==8 && out.remainingSeconds==0 && out.bytesPerSecond==4);
}
''')
resume_setup = section('far2l/src/copy.cpp', '\tif (Resume) {\n\t\t_ResumeOffset =', '\n}\n\nShellFileTransfer::~ShellFileTransfer()')
resume_rollback = section('far2l/src/copy.cpp', '\t\t\t_ProgressState.TotalResumeOffset-=', '\n\t\t\t_ProgressState.Progress->')
check('Resume prefix accounting across success, cancellation, retry and append', r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
using std::min;
#define Min min
struct State { uint64_t CurResumeOffset=0, TotalResumeOffset=32; };
struct Transfer {
    State &_ProgressState;
    uint64_t _ResumeOffset=0;
    Transfer(State &state, bool Resume, int64_t _AppendPos):_ProgressState(state) {
        struct { uint64_t nFileSize=96; } SrcData;
        // ShellCopyFile resets the per-file offset before every attempt.
        _ProgressState.CurResumeOffset=0;
''' + resume_setup + r'''
    }
    void Cancel() {
''' + resume_rollback + r'''
    }
};
int main() {
    State state;
    Transfer completed(state,true,64);
    assert(state.CurResumeOffset==64 && state.TotalResumeOffset==96);
    Transfer partial(state,true,48);
    assert(state.CurResumeOffset==48 && state.TotalResumeOffset==144);
    partial.Cancel();
    assert(state.CurResumeOffset==0 && state.TotalResumeOffset==96);
    Transfer retry(state,true,48);
    assert(state.CurResumeOffset==48 && state.TotalResumeOffset==144);
    retry.Cancel();
    assert(state.TotalResumeOffset==96);
    Transfer append(state,false,64);
    assert(state.CurResumeOffset==0 && state.TotalResumeOffset==96);
    Transfer oversized(state,true,128);
    assert(state.CurResumeOffset==96 && state.TotalResumeOffset==192);
    oversized.Cancel(); assert(state.TotalResumeOffset==96);
}
''')

# A successful copy while moving symlink contents must not credit the resumed prefix twice.
symlink_accounting = section('far2l/src/copy.cpp', '\t\t\t\t\t\t\t\t\tif (!NeedRename)',
                             '\n\t\t\t\t\t\t\t\t\t\tcontinue;')
remaining = section('far2l/src/copy.cpp', 'static uint64_t CopySizeLeft(', '\nclass CopyProgress final')
check('Resumed symlink-move accounting', r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
using std::min;
#define Min min
''' + remaining + r'''
int main()
{
    for (uint64_t prefix : {uint64_t(0),uint64_t(80),uint64_t(100)}) {
        const bool NeedRename=false;
        struct { uint64_t nFileSize=100; } SrcData;
        struct {
            uint64_t CurResumeOffset,CurCopiedSize,TotalCopiedSize,TotalSkippedSize;
        } ProgressState{prefix,100-prefix,25+100-prefix,5};
''' + symlink_accounting + r'''
        }
        assert(ProgressState.TotalCopiedSize==25+100-prefix);
        assert(ProgressState.TotalSkippedSize==5);
        // Other files still remain; the resumed prefix must not finish the operation early.
        assert(CopySizeLeft(200,ProgressState.TotalCopiedSize,prefix)==75);
    }
}
''')
