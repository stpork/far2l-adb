from pathlib import Path
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'testing/perf-regressions'))
from common import check, section

backup = section('plugins/memo/src/memo.cpp', 'static bool ReadTextFile(', 'static bool UpdateGlobalMacro(')
save = section('plugins/memo/src/memo.cpp', 'static bool SaveFile(', 'static std::wstring NormalizeMemoContent(')
switch = section('plugins/memo/src/memo.cpp', 'static void SwitchToMemo(', '// Expand a leading')
close = section('plugins/memo/src/memo.cpp', '  if (Msg == DN_CLOSE)', '  return g_far.DefDlgProc')
check('Memo preserves disk and editor text after a short write', r'''
#include "utils/src/StagedFile.cpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <signal.h>
#include <sys/resource.h>
#define DBG(...) ((void)0)
using HANDLE = int;
using LONG_PTR = long;
struct FarDialogItem { bool DefaultButton; };
enum { DM_SETTEXTPTR, DM_GETDLGITEMSHORT, DM_SETDLGITEMSHORT, DM_REDRAW, DM_SETFOCUS };
enum { MEMO_COUNT = 2, DI_MEMO = 3, DI_BTN1, DN_CLOSE, FALSE = 0 };
struct Far {
    int writes = 0;
    int SendDlgMessage(HANDLE, int msg, int, LONG_PTR) {
        if (msg == DM_SETTEXTPTR) ++writes;
        return 0;
    }
} g_far;
int g_currentMemo = 0;
std::wstring g_loadedContent = L"original";
std::wstring unsaved(4096, L'x');
int save_errors = 0;
std::wstring GetMemoFilePath(int index) { return index ? L"next.txt" : L"memo.txt"; }
std::string Wide2MB(const wchar_t* s) { std::wstring w(s); return {w.begin(), w.end()}; }
void ShowSaveError(const wchar_t*, int error) { assert(error); ++save_errors; }
std::wstring GetMemoText(HANDLE) { return unsaved; }
bool unreadable = false;
bool LoadFile(int, std::wstring& result) {
    if (unreadable) return false;
    result = L"next";
    return true;
}
void SetMemoTitle(HANDLE) {}
void SaveLastMemo(int) {}
static std::string ReadMemo() {
    std::ifstream file("memo.txt");
    return {std::istreambuf_iterator<char>(file), {}};
}
''' + backup + save + switch + 'int Close(HANDLE hDlg, int Msg) {\n' + close + r'''
    return 1;
}
int main(int argc, char** argv) {
    assert(argc == 2 && chdir(argv[1]) == 0);
    assert(SaveFile(0, g_loadedContent));
    std::ofstream("macros.ini") << std::string(4096, 'b');
    signal(SIGXFSZ, SIG_IGN);
    struct rlimit old{}, limited{};
    assert(getrlimit(RLIMIT_FSIZE, &old) == 0);
    limited = old;
    limited.rlim_cur = 512;
    assert(setrlimit(RLIMIT_FSIZE, &limited) == 0);
    assert(!BackupMacrosFile("macros.ini"));
    assert(access("macros.bak", F_OK) != 0);
    SwitchToMemo(0, 1);
    assert(g_currentMemo == 0 && g_far.writes == 0 && save_errors == 1);
    assert(Close(0, DN_CLOSE) == FALSE && save_errors == 2);
    assert(ReadMemo() == "original" && unsaved.size() == 4096);
    assert(setrlimit(RLIMIT_FSIZE, &old) == 0);
    assert(BackupMacrosFile("macros.ini"));
    std::string backup;
    assert(ReadTextFile("macros.bak", backup, false) && backup == std::string(4096, 'b'));
    unreadable = true;
    SwitchToMemo(0, 1);
    assert(g_currentMemo == 0 && g_far.writes == 0 && ReadMemo() == "original");
    unreadable = false;
    SwitchToMemo(0, 1);
    assert(g_currentMemo == 1 && g_far.writes == 1);
    assert(ReadMemo() == std::string(4096, 'x'));
}
''')
