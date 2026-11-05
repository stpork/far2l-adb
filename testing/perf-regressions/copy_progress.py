from common import section, check

setup = section('far2l/src/copy.cpp', '\t// нужно ли показывать время копирования?',
                '\n\tif (Background) {')
snapshot = section('far2l/src/copy.cpp', 'struct CopyProgressSnapshot\n', '\nclass CopyProgress ')
get_snapshot = section('far2l/src/copy.cpp', 'void CopyProgress::GetSnapshot(',
                       '\nvoid CopyProgress::SetFileCounts(')

check('Single-file copy progress mode and speed', r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <mutex>
#include <string>
using DWORD = uint32_t;
using std::min;
#define Min min
DWORD now = 7000;
DWORD GetProcessUptimeMSec() { return now; }
constexpr unsigned COPY_RULE_FILES = 1;
struct { struct { unsigned CopyTimeRule = COPY_RULE_FILES; } CMOpt; } Opt;
struct State { bool ShowTotalCopySize = true; int TotalFilesToProcess = 0; };
void PrepareCopy(State &ProgressState, int count, bool folder)
{
    struct { int SelCount; bool FolderPresent; } CDP{count, folder};
''' + setup + r'''
}
''' + snapshot + r'''
struct CopyProgress
{
    mutable std::mutex StateMutex;
    std::wstring CurrentSource = L"source", CurrentDestination = L"destination";
    uint64_t CurrentCompleted = 256ull * 1024 * 1024, CurrentTotal = 1024ull * 1024 * 1024;
    uint64_t OverallCompleted = 0, OverallTotal = 0, OverallSkipped = 0;
    int FilesProcessed = 0, FilesTotal = 1;
    bool Move = false, Total, Time = true, Scanning = false;
    DWORD BackgroundStartTime = 1000;
    CopyProgress(bool total): Total(total) {}
    void GetSnapshot(CopyProgressSnapshot &snapshot) const;
};
''' + get_snapshot + r'''
int main()
{
    State single;
    PrepareCopy(single, 1, false);
    CopyProgress progress(single.ShowTotalCopySize);
    CopyProgressSnapshot snapshot;
    progress.GetSnapshot(snapshot);
    assert(!snapshot.showTotal && single.TotalFilesToProcess == 1);
    assert(snapshot.currentCompleted == 256ull * 1024 * 1024);
    assert(snapshot.bytesPerSecond == snapshot.currentCompleted / 6);
    assert(snapshot.remainingSeconds == 18);

    // Reopening a background operation uses the same progress mode and counters.
    now += 6000;
    progress.CurrentCompleted *= 2;
    progress.GetSnapshot(snapshot);
    assert(!snapshot.showTotal && snapshot.bytesPerSecond == progress.CurrentCompleted / 12);
    assert(snapshot.remainingSeconds == 12);

    for (bool folder : {false, true}) {
        State batch;
        PrepareCopy(batch, folder ? 1 : 2, folder);
        CopyProgress total(batch.ShowTotalCopySize);
        total.OverallTotal = 4ull * 1024 * 1024 * 1024;
        total.OverallCompleted = 2ull * 1024 * 1024 * 1024;
        total.OverallSkipped = 1024ull * 1024 * 1024;
        total.GetSnapshot(snapshot);
        assert(snapshot.showTotal && snapshot.overallTotal == total.OverallTotal);
        assert(snapshot.bytesPerSecond == (total.OverallCompleted - total.OverallSkipped) / 12);
        assert(snapshot.remainingSeconds == 24);
    }

    State disabled;
    disabled.ShowTotalCopySize = false;
    PrepareCopy(disabled, 2, false);
    assert(!disabled.ShowTotalCopySize);
}
''')
