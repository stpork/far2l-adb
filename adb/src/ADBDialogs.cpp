// Native-parity rewrite ported from mtp/src/MTPDialogs.cpp. Brings the
// ADB plugin's Copy/Move/Delete/Overwrite dialogs in line with far2l's
// native CopyProgress / ShellDeleteMsg / WarnCopyDlg layouts (sizes,
// element placement, indicators, button structure, control logic).
// Kept structurally identical to MTPDialogs so future tweaks can be
// cherry-picked across.

#include "ADBDialogs.h"
#include "ADBLog.h"
#include "farplug-wide.h"
#include <utils.h>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <iomanip>

extern PluginStartupInfo g_Info;
extern FarStandardFunctions g_FSF;

static std::wstring FormatTimeLong(uint64_t total_seconds) {
    if (total_seconds > 999999) total_seconds = 999999;
    uint64_t hours = total_seconds / 3600;
    uint64_t minutes = (total_seconds % 3600) / 60;
    uint64_t seconds = total_seconds % 60;
    std::wostringstream out;
    out << std::setw(2) << std::setfill(L'0') << hours << L":"
        << std::setw(2) << std::setfill(L'0') << minutes << L":"
        << std::setw(2) << std::setfill(L'0') << seconds;
    return out.str();
}

static std::wstring FormatBytes(uint64_t bytes) {
    static const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    size_t unit = 0;
    double value = static_cast<double>(bytes);
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    std::wostringstream out;
    if (unit == 0) {
        out << bytes << L" " << units[unit];
    } else {
        out << std::fixed << std::setprecision(2) << value << L" " << units[unit];
    }
    return out.str();
}

// Native far2l Total separator format: raw byte count with thousands
// separated by spaces. Example: "2 761 173 732".
static std::wstring FormatBytesWithSpaces(uint64_t bytes) {
    std::wstring s = std::to_wstring(bytes);
    if (s.size() <= 3) return s;
    std::wstring out;
    out.reserve(s.size() + s.size() / 3);
    int n = static_cast<int>(s.size());
    for (int i = 0; i < n; ++i) {
        if (i > 0 && ((n - i) % 3) == 0) out.push_back(L' ');
        out.push_back(s[i]);
    }
    return out;
}

// Native CopyTimeInfo speed format: "<value> <prefix>b/s" — lowercase
// 'b' is literal in the native format string ("%8.8lsb/s").
static std::wstring FormatSpeed(uint64_t bytes_per_sec) {
    if (bytes_per_sec == 0) return L"- b/s";
    static const wchar_t* units[] = {L"", L"K", L"M", L"G"};
    size_t unit = 0;
    double value = static_cast<double>(bytes_per_sec);
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    std::wostringstream out;
    if (unit == 0) {
        out << bytes_per_sec << L" b/s";
    } else {
        out << std::fixed << std::setprecision(2) << value << L" " << units[unit] << L"b/s";
    }
    return out.str();
}

static std::wstring AbbreviatePathLeft(const std::wstring& path, size_t max_len) {
    if (path.size() <= max_len) return path;
    if (max_len < 6) return path.substr(path.size() - max_len);
    return L"..." + path.substr(path.size() - max_len + 3);
}

static std::wstring AbbreviatePathRight(const std::wstring& path, size_t max_len) {
    if (path.size() <= max_len) return path;
    if (max_len < 6) return path.substr(0, max_len);
    return path.substr(0, max_len - 3) + L"...";
}

const wchar_t* FarDialogItems::MB2WidePooled(const char* sz) {
    if (!sz) return nullptr;
    if (!*sz) return L"";
    MB2Wide(sz, _str_pool_tmp);
    return _str_pool.emplace(_str_pool_tmp).first->c_str();
}

const wchar_t* FarDialogItems::WidePooled(const wchar_t* wz) {
    if (!wz) return nullptr;
    if (!*wz) return L"";
    return _str_pool.emplace(wz).first->c_str();
}

int FarDialogItems::AddInternal(int type, int x1, int y1, int x2, int y2, unsigned int flags, const wchar_t* data) {
    int index = (int)size();
    resize(index + 1);
    auto& item = back();
    item.Type = type;
    item.X1 = x1;
    item.Y1 = y1;
    item.X2 = x2;
    item.Y2 = y2;
    item.Focus = 0;
    item.History = nullptr;
    item.Flags = flags;
    item.DefaultButton = 0;
    item.PtrData = data;
    if (index > 0) {
        auto& box = operator[](0);
        if (box.Y2 < y2 + 1) box.Y2 = y2 + 1;
        if (box.X2 < x2 + 2) box.X2 = x2 + 2;
    }
    return index;
}

FarDialogItems::FarDialogItems() {
    AddInternal(DI_DOUBLEBOX, 3, 1, 5, 3, 0, L"");
}

int FarDialogItems::SetBoxTitleItem(const wchar_t* title) {
    if (empty()) return -1;
    operator[](0).PtrData = WidePooled(title);
    return 0;
}

int FarDialogItems::Add(int type, int x1, int y1, int x2, int y2, unsigned int flags, const wchar_t* data) {
    return AddInternal(type, x1, y1, x2, y2, flags, WidePooled(data));
}

int FarDialogItems::Add(int type, int x1, int y1, int x2, int y2, unsigned int flags, const char* data) {
    return AddInternal(type, x1, y1, x2, y2, flags, MB2WidePooled(data));
}

int FarDialogItems::EstimateWidth() const {
    int min_x = 0, max_x = 0;
    for (const auto& item : *this) {
        if (min_x > item.X1 || &item == &front()) min_x = item.X1;
        if (max_x < item.X1 || &item == &front()) max_x = item.X1;
        if (max_x < item.X2) max_x = item.X2;
    }
    return max_x + 1 - min_x;
}

int FarDialogItems::EstimateHeight() const {
    int min_y = 0, max_y = 0;
    for (const auto& item : *this) {
        if (min_y > item.Y1 || &item == &front()) min_y = item.Y1;
        if (max_y < item.Y1 || &item == &front()) max_y = item.Y1;
        if (max_y < item.Y2) max_y = item.Y2;
    }
    return max_y + 1 - min_y;
}

void FarDialogItemsLineGrouped::SetLine(int y) { _y = y; }
void FarDialogItemsLineGrouped::NextLine() { ++_y; }

int FarDialogItemsLineGrouped::AddAtLine(int type, int x1, int x2, unsigned int flags, const char* data) {
    return Add(type, x1, _y, x2, _y, flags, data);
}

int FarDialogItemsLineGrouped::AddAtLine(int type, int x1, int x2, unsigned int flags, const wchar_t* data) {
    return Add(type, x1, _y, x2, _y, flags, data);
}

BaseDialog::~BaseDialog() {
    if (_dlg != INVALID_HANDLE_VALUE) {
        g_Info.DialogFree(_dlg);
    }
}

LONG_PTR BaseDialog::sSendDlgMessage(HANDLE dlg, int msg, int param1, LONG_PTR param2) {
    return g_Info.SendDlgMessage(dlg, msg, param1, param2);
}

LONG_PTR BaseDialog::SendDlgMessage(int msg, int param1, LONG_PTR param2) {
    return sSendDlgMessage(_dlg, msg, param1, param2);
}

LONG_PTR WINAPI BaseDialog::sDlgProc(HANDLE dlg, int msg, int param1, LONG_PTR param2) {
    BaseDialog* it = (BaseDialog*)sSendDlgMessage(dlg, DM_GETDLGDATA, 0, 0);
    if (it && dlg == it->_dlg) {
        return it->DlgProc(msg, param1, param2);
    }
    return g_Info.DefDlgProc(dlg, msg, param1, param2);
}

LONG_PTR BaseDialog::DlgProc(int msg, int param1, LONG_PTR param2) {
    return g_Info.DefDlgProc(_dlg, msg, param1, param2);
}

int BaseDialog::Show(const wchar_t* help_topic, int extra_width, int extra_height, unsigned int flags) {
    if (_dlg == INVALID_HANDLE_VALUE) {
        DBG("BaseDialog::Show DialogInit help=%ls items=%zu w=%d h=%d flags=0x%x",
            help_topic, _di.size(),
            _di.EstimateWidth() + extra_width, _di.EstimateHeight() + extra_height, flags);
        _dlg = g_Info.DialogInit(g_Info.ModuleNumber, -1, -1,
            _di.EstimateWidth() + extra_width, _di.EstimateHeight() + extra_height,
            help_topic, &_di[0], _di.size(), 0, flags, &sDlgProc, (LONG_PTR)(uintptr_t)this);
        DBG("BaseDialog::Show DialogInit returned dlg=%p", (void*)_dlg);
        if (_dlg == INVALID_HANDLE_VALUE) return -2;
    }
    int rc = g_Info.DialogRun(_dlg);
    DBG("BaseDialog::Show DialogRun returned rc=%d", rc);
    return rc;
}

void BaseDialog::Close(int code) {
    SendDlgMessage(DM_CLOSE, code, 0);
}

void BaseDialog::SetDefaultDialogControl(int ctl) {
    if (ctl == -1) {
        if (!_di.empty()) SetDefaultDialogControl((int)(_di.size() - 1));
        return;
    }
    for (size_t i = 0; i < _di.size(); ++i) {
        _di[i].DefaultButton = ((int)i == ctl) ? 1 : 0;
    }
}

void BaseDialog::SetFocusedDialogControl(int ctl) {
    if (ctl == -1) {
        if (!_di.empty()) SetFocusedDialogControl((int)(_di.size() - 1));
        return;
    }
    for (size_t i = 0; i < _di.size(); ++i) {
        _di[i].Focus = ((int)i == ctl) ? 1 : 0;
    }
}

void BaseDialog::TextToDialogControl(int ctl, const std::wstring& str) {
    if (ctl < 0 || (size_t)ctl >= _di.size()) return;
    if (_dlg == INVALID_HANDLE_VALUE) {
        _di[ctl].PtrData = _di.WidePooled(str.c_str());
        return;
    }
    FarDialogItemData dd = { str.size(), (wchar_t*)str.c_str() };
    SendDlgMessage(DM_SETTEXT, ctl, (LONG_PTR)&dd);
}

void BaseDialog::TextToDialogControl(int ctl, const char* str) {
    if (!str) return;
    std::wstring tmp;
    MB2Wide(str, tmp);
    TextToDialogControl(ctl, tmp);
}

void BaseDialog::ProgressBarToDialogControl(int ctl, int percents) {
    if (ctl < 0 || (size_t)ctl >= _di.size()) return;
    if (_progress_bg == 0) {
        if (g_FSF.BoxSymbols) _progress_bg = g_FSF.BoxSymbols[BS_X_B0];
        if (_progress_bg == 0) _progress_bg = L'.';
    }
    if (_progress_fg == 0) {
        if (g_FSF.BoxSymbols) _progress_fg = g_FSF.BoxSymbols[BS_X_DB];
        if (_progress_fg == 0) _progress_fg = L'#';
    }
    std::wstring str;
    int width = _di[ctl].X2 + 1 - _di[ctl].X1;
    str.resize(width);
    if (percents >= 0) {
        int filled = (percents * width) / 100;
        for (int i = 0; i < filled; ++i) str[i] = _progress_fg;
        for (int i = filled; i < width; ++i) str[i] = _progress_bg;
    } else {
        for (auto& c : str) c = L'-';
    }
    TextToDialogControl(ctl, str);
}

AbortConfirmDialog::AbortConfirmDialog() {
    _di.SetBoxTitleItem(L"Abort operation");
    _di.SetLine(2);
    _di.AddAtLine(DI_TEXT, 5, 48, DIF_CENTERGROUP, L"Confirm abort current operation");
    _di.NextLine();
    _di.AddAtLine(DI_TEXT, 4, 49, DIF_BOXCOLOR | DIF_SEPARATOR);
    _di.NextLine();
    _i_confirm = _di.AddAtLine(DI_BUTTON, 6, 27, DIF_CENTERGROUP, L"&Abort operation");
    _i_cancel = _di.AddAtLine(DI_BUTTON, 32, 45, DIF_CENTERGROUP, L"&Continue");
    SetFocusedDialogControl(_i_cancel);
    SetDefaultDialogControl(_i_cancel);
}

LONG_PTR AbortConfirmDialog::DlgProc(int msg, int param1, LONG_PTR param2) {
    if (msg == DM_KEY && param2 == 0x1b) {
        Close(_i_cancel);
        return TRUE;
    }
    return BaseDialog::DlgProc(msg, param1, param2);
}

bool AbortConfirmDialog::Ask() {
    int reply = Show(L"ADBAbortConfirm", 6, 2, FDLG_WARNING);
    return (reply == _i_confirm || reply < 0);
}

static std::wstring FormatFileInfo(const wchar_t* label, uint64_t size, int64_t mtime) {
    // Native far2l format (copy.cpp:3090): label left-aligned and
    // expanded to 17 cells, size right-aligned in 25 cells, then date
    // and time. Date format DD-MM-YYYY matches FarEng.lng screenshot
    // output from the reference dialogs.
    std::wostringstream out;
    out << label;
    int pad = 17 - (int)wcslen(label);
    if (pad < 1) pad = 1;
    out << std::wstring(pad, L' ');
    std::wstring sz = std::to_wstring(size);
    if (sz.size() < 25) out << std::wstring(25 - sz.size(), L' ');
    out << sz;
    if (mtime > 0) {
        std::time_t t = static_cast<std::time_t>(mtime);
        std::tm tm{};
        if (localtime_r(&t, &tm)) {
            wchar_t buf[64];
            swprintf(buf, sizeof(buf)/sizeof(buf[0]),
                     L" %02d-%02d-%04d %02d:%02d:%02d",
                     tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900,
                     tm.tm_hour, tm.tm_min, tm.tm_sec);
            out << buf;
        }
    }
    return out.str();
}

OverwriteDialog::OverwriteDialog(const std::wstring& dest_name,
                                 uint64_t src_size, int64_t src_mtime,
                                 uint64_t dst_size, int64_t dst_mtime) {
    // Native WARN_DLG_WIDTH=88, WARN_DLG_HEIGHT=13 (copy.cpp:3014).
    // Box X1=3, X2=84 (88-4) — 82-cell inner area. Content items at
    // X1=5..X2=82 (= WARN_DLG_WIDTH-6). Separators use X2=0 for FAR's
    // auto-stretch with proper border-corner joins; AddInternal won't
    // grow the box past X2=84 because (0+2) < 84. Filename rendered
    // as DI_TEXT (not DI_EDIT) so it shares the dialog background;
    // DI_EDIT in plugin SDK paints the editor color across the whole
    // field which doesn't match native's WarnCopyDlg appearance.
    _di.SetBoxTitleItem(L"Warning");
    _di.SetLine(2);
    _di.AddAtLine(DI_TEXT, 5, 82, DIF_CENTERGROUP, L"File already exists");
    _di.NextLine();
    _i_filename = _di.AddAtLine(DI_TEXT, 5, 82, 0,
                                AbbreviatePathLeft(dest_name, 78).c_str());
    _di.NextLine();
    _di.AddAtLine(DI_TEXT, 3, 0, DIF_BOXCOLOR | DIF_SEPARATOR);
    _di.NextLine();
    _i_src_info = _di.AddAtLine(DI_BUTTON, 5, 82,
                                DIF_BTNNOCLOSE | DIF_NOBRACKETS,
                                FormatFileInfo(L"New", src_size, src_mtime).c_str());
    _di.NextLine();
    _i_dst_info = _di.AddAtLine(DI_BUTTON, 5, 82,
                                DIF_BTNNOCLOSE | DIF_NOBRACKETS,
                                FormatFileInfo(L"Existing", dst_size, dst_mtime).c_str());
    _di.NextLine();
    _di.AddAtLine(DI_TEXT, 3, 0, DIF_BOXCOLOR | DIF_SEPARATOR);
    _di.NextLine();
    _i_remember = _di.AddAtLine(DI_CHECKBOX, 5, 30, 0, L"&Remember choice");
    _di.NextLine();
    _di.AddAtLine(DI_TEXT, 3, 0, DIF_BOXCOLOR | DIF_SEPARATOR);
    _di.NextLine();
    _i_overwrite = _di.AddAtLine(DI_BUTTON, 0, 0, DIF_CENTERGROUP, L"&Overwrite");
    _i_skip      = _di.AddAtLine(DI_BUTTON, 0, 0, DIF_CENTERGROUP, L"&Skip");
    _i_cancel    = _di.AddAtLine(DI_BUTTON, 0, 0, DIF_CENTERGROUP, L"&Cancel");
    SetFocusedDialogControl(_i_remember);
    SetDefaultDialogControl(_i_overwrite);
}

LONG_PTR OverwriteDialog::DlgProc(int msg, int param1, LONG_PTR param2) {
    if (msg == DM_KEY && param2 == 0x1b) {
        Close(_i_cancel);
        return TRUE;
    }
    return BaseDialog::DlgProc(msg, param1, param2);
}

OverwriteDialog::Result OverwriteDialog::Ask() {
    // extra_width=3, extra_height=2 → outer 88x13 (= native WARN_DLG_*).
    // 3 cells right padding + 1 row bottom padding match the etalon.
    int reply = Show(L"ADBOverwrite", 3, 2, FDLG_WARNING);
    const bool remember = (SendDlgMessage(DM_GETCHECK, _i_remember, 0) == BSTATE_CHECKED);
    if (reply == _i_overwrite) return remember ? OVERWRITE_ALL : OVERWRITE;
    if (reply == _i_skip)      return remember ? SKIP_ALL : SKIP;
    return CANCEL;
}

ProgressDialog::ProgressDialog(ProgressState& state, bool is_move, bool is_multi)
    : _state(state), _is_move(is_move), _is_multi(is_multi) {
    InitLayout();
}

void ProgressDialog::InitLayout() {
    // Mirrors far2l CopyProgress::CreateBackground (copy.cpp:312)
    // line-for-line. Single-file (Total=false) gets 9 content rows;
    // multi-file (Total=true) inserts " Total <bytes> " separator and
    // a second bar — 11 content rows. BarSize=52 cells, no Cancel
    // button, Esc-only abort — all matching native exactly.
    _di.SetBoxTitleItem(_is_move ? L"Rename/Move" : L"Copy");
    _di.SetLine(2);
    _i_verb = _di.AddAtLine(DI_TEXT, 5, 60, 0,
                            _is_move ? L"Moving the file" : L"Copying the file");
    _di.NextLine();
    _i_from_path = _di.AddAtLine(DI_TEXT, 5, 60, 0, L"");
    _di.NextLine();
    _di.AddAtLine(DI_TEXT, 5, 7, 0, L"to");
    _di.NextLine();
    _i_to_path = _di.AddAtLine(DI_TEXT, 5, 60, 0, L"");
    _di.NextLine();
    _i_progress_bar = _di.AddAtLine(DI_TEXT, 5, 56, 0);     // 52-cell bar
    _i_percent = _di.AddAtLine(DI_TEXT, 57, 61, 0, L"0%");  // 5-cell "100%"
    _di.NextLine();
    if (_is_multi) {
        _i_total_bytes = _di.AddAtLine(DI_TEXT, 3, 63, DIF_BOXCOLOR | DIF_SEPARATOR, L"");
        _di.NextLine();
        _i_total_bar = _di.AddAtLine(DI_TEXT, 5, 56, 0);    // 52-cell total bar
        _di.NextLine();
    }
    _di.AddAtLine(DI_TEXT, 3, 63, DIF_BOXCOLOR | DIF_SEPARATOR);
    _di.NextLine();
    _i_files_processed = _di.AddAtLine(DI_TEXT, 5, 60, 0, L"");
    _di.NextLine();
    _di.AddAtLine(DI_TEXT, 3, 63, DIF_BOXCOLOR | DIF_SEPARATOR);
    _di.NextLine();
    _i_time = _di.AddAtLine(DI_TEXT, 5, 60, 0, L"");
    // Cap box back to natural width — separators at X2=63 grew it to
    // 65, leaving 2 dead cells at the right edge that visually shift
    // the dialog wider than native every redraw.
    _di[0].X2 = 63;
}

void ProgressDialog::Show() {
    while (!_state.finished) {
        _finished = false;
        BaseDialog::Show(L"ADBProgress", 2, 1, FDLG_REGULARIDLE);
        if (_finished) break;
        if (ShowAbortConfirmation()) {
            _state.SetAborting();
            break;
        }
    }
}

bool ProgressDialog::ShowAbortConfirmation() {
    if (_state.IsAborting()) return true;
    AbortConfirmDialog dlg;
    return dlg.Ask();
}

LONG_PTR ProgressDialog::DlgProc(int msg, int param1, LONG_PTR param2) {
    if (msg == DN_ENTERIDLE) {
        if (_state.finished) {
            if (!_finished) {
                _finished = true;
                Close();
            }
        } else {
            UpdateDialog();
        }
    } else if (msg == DM_KEY && param2 == 0x1b) {
        // Native far2l copy uses Esc to abort (CheckForEscSilent +
        // ConfirmAbortOp). Mirror that here.
        if (ShowAbortConfirmation()) {
            _state.SetAborting();
            Close();
        }
        return TRUE;
    }
    return BaseDialog::DlgProc(msg, param1, param2);
}

void ProgressDialog::UpdateDialog() {
    std::wstring source_path, dest_path, current_file;
    {
        std::lock_guard<std::mutex> locker(_state.mtx_strings);
        source_path = _state.source_path;
        dest_path = _state.dest_path;
        current_file = _state.current_file;
    }
    const uint64_t all_complete = _state.all_complete.load();
    const uint64_t all_total = _state.all_total.load();
    const uint64_t count_complete = _state.count_complete.load();
    const uint64_t count_total = _state.count_total.load();
    const uint64_t file_complete_bytes = _state.file_complete.load();
    const uint64_t file_total_bytes = _state.file_total.load();

    // File-level bar — bytes when known, else falls back to count
    // progress so it animates even for backends that don't report size.
    int file_percent = 0;
    if (file_total_bytes > 0 && file_complete_bytes <= file_total_bytes) {
        file_percent = static_cast<int>((file_complete_bytes * 100) / file_total_bytes);
    } else if (all_total > 0 && all_complete <= all_total) {
        file_percent = static_cast<int>((all_complete * 100) / all_total);
    } else if (count_total > 0) {
        file_percent = static_cast<int>((count_complete * 100) / count_total);
    }
    if (file_percent > 100) file_percent = 100;

    // Total bar — only meaningful for multi-file ops. For a single
    // file we mirror the file bar so the dialog still feels alive.
    int total_percent = file_percent;
    if (count_total > 1) {
        if (all_total > 0) {
            total_percent = static_cast<int>(std::min<uint64_t>(100, (all_complete * 100) / all_total));
        } else {
            total_percent = static_cast<int>(std::min<uint64_t>(100, (count_complete * 100) / count_total));
        }
    }

    // Compose the from/to lines. Source/dest paths are panel-relative;
    // append the current item so the user sees what's flowing right now.
    std::wstring full_from = source_path;
    std::wstring full_to = dest_path;
    if (!current_file.empty()) {
        if (!full_from.empty() && full_from.back() != L'/') full_from += L'/';
        full_from += current_file;
        if (!full_to.empty() && full_to.back() != L'/' && full_to.back() != L'\\') full_to += L'/';
        full_to += current_file;
    }

    const bool path_changed = _first_update || full_from != _last_from || full_to != _last_to;
    const bool progress_changed = _first_update || all_complete != _last_complete
                                  || all_total != _last_total
                                  || file_percent != _last_file_percent
                                  || total_percent != _last_total_percent;
    const bool count_changed = _first_update || count_complete != _last_count;

    if (path_changed) {
        _last_from = full_from;
        _last_to = full_to;
        // Truncate to the exact item width (56 cells = X1=5..X2=60)
        // so the displayed text never overflows the row; otherwise
        // the longer strings spill past the right edge and the frame
        // visually "shifts" between updates.
        TextToDialogControl(_i_from_path, AbbreviatePathLeft(full_from, 56));
        TextToDialogControl(_i_to_path, AbbreviatePathRight(full_to, 56));
    }
    if (progress_changed) {
        _last_complete = all_complete;
        _last_total = all_total;
        _last_file_percent = file_percent;
        _last_total_percent = total_percent;
        ProgressBarToDialogControl(_i_progress_bar, file_percent);
        TextToDialogControl(_i_percent, std::to_wstring(file_percent) + L"%");
        if (_is_multi) {
            // Native CopyProgress puts " Total: <raw-bytes-with-spaces> "
            // in the separator (copy.cpp:331). Raw byte count with
            // thousands separated by spaces, "Total:" with colon.
            ProgressBarToDialogControl(_i_total_bar, total_percent);
            std::wstring bytes_str = L" Total ";
            if (all_total > 0) {
                bytes_str = L" Total: " + FormatBytesWithSpaces(all_total) + L" ";
            } else if (file_total_bytes > 0) {
                bytes_str = L" Total: " + FormatBytesWithSpaces(file_total_bytes) + L" ";
            }
            TextToDialogControl(_i_total_bytes, bytes_str);
        }
    }
    if (count_changed) {
        _last_count = count_complete;
        // Msg::CopyProcessed / CopyProcessedTotal: single-file shows
        // just "Files processed: N", multi shows "Files processed: N of M".
        std::wstring counter = L"Files processed: " + std::to_wstring(count_complete);
        if (count_total > 1) counter += L" of " + std::to_wstring(count_total);
        TextToDialogControl(_i_files_processed, counter);
    }
    _first_update = false;

    // Msg::CopyTimeInfo = "Time: %8.8ls    Remaining: %8.8ls    %8.8lsb/s"
    const auto now = std::chrono::steady_clock::now();
    int64_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - _state.start_time).count();
    if (elapsed_ms < 1) elapsed_ms = 1;
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - _prev_ts).count() >= 1000
        || _prev_ts.time_since_epoch().count() == 0) {
        if (all_complete > 0) _speed = (all_complete * 1000) / elapsed_ms;
        _prev_ts = now;
    }
    std::wstring remaining_str = L"??:??:??";
    if (_speed > 0 && all_complete < all_total && all_total > 0) {
        remaining_str = FormatTimeLong((all_total - all_complete) / _speed);
    } else if (file_percent > 0 && file_percent < 100) {
        remaining_str = FormatTimeLong((((uint64_t)elapsed_ms * 100) / file_percent - elapsed_ms) / 1000);
    }
    std::wstring time_str = FormatTimeLong(elapsed_ms / 1000);
    std::wstring speed_str = FormatSpeed(_speed);
    TextToDialogControl(_i_time,
        L"Time: " + time_str + L"    Remaining: " + remaining_str + L"    " + speed_str);
}

// Wakes the modal dialog so DN_ENTERIDLE fires. FDLG_REGULARIDLE
// alone is not sufficient on macOS far2l — the dialog only sees one
// idle event (when the worker writes the final NOOP) without this.
static void PostNoopEvent() {
    INPUT_RECORD ir = {};
    ir.EventType = NOOP_EVENT;
    DWORD dw = 0;
    WINPORT(WriteConsoleInput)(0, &ir, 1, &dw);
}

// Shared plumbing for any "background worker + modal progress dialog"
// pair. Spawns a worker that runs `work` against `state`, a 250 ms
// ticker that keeps the dialog repainting, then runs `show_dialog` on
// the calling thread (which must drive the modal until state.finished).
// Joins both threads before returning.
static void RunModalProgress(std::shared_ptr<ProgressState> state,
                             std::function<void(ProgressState&)> work,
                             std::function<void()> show_dialog) {
    std::thread worker([state, work = std::move(work)]() {
        try { work(*state); } catch (...) {}
        state->SetFinished();
        PostNoopEvent();
    });
    std::atomic<bool> stop{false};
    std::thread ticker([state, &stop]() {
        while (!stop.load() && !state->IsFinished()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (stop.load() || state->IsFinished()) break;
            PostNoopEvent();
        }
    });
    show_dialog();
    stop.store(true);
    if (ticker.joinable()) ticker.join();
    if (worker.joinable()) worker.join();
}

ProgressOperation::ProgressOperation(bool is_move, bool is_multi)
    : _state(std::make_shared<ProgressState>()), _is_move(is_move), _is_multi(is_multi) {
    _state->Reset();
}

void ProgressOperation::Run(WorkFunc work_func) {
    RunModalProgress(_state, std::move(work_func), [this]() {
        ProgressDialog dlg(*_state, _is_move, _is_multi);
        dlg.Show();
    });
}

DeleteProgressDialog::DeleteProgressDialog(ProgressState& state)
    : _state(state) {
    // Mirrors far2l ShellDeleteMsg (delete.cpp): inner width 52,
    // title Msg::DeleteTitle = "Delete", line Msg::Deleting =
    // "Deleting the file or folder", then the filename centered.
    _di.SetBoxTitleItem(L"Delete");
    _di.SetLine(2);
    _di.AddAtLine(DI_TEXT, 5, 56, DIF_CENTERGROUP, L"Deleting the file or folder");
    _di.NextLine();
    _i_filename = _di.AddAtLine(DI_TEXT, 5, 56, DIF_CENTERGROUP, L"");
}

void DeleteProgressDialog::Show() {
    while (!_state.finished) {
        _finished = false;
        BaseDialog::Show(L"ADBProgress", 2, 1, FDLG_REGULARIDLE);
        if (_finished) break;
        if (ShowAbortConfirmation()) {
            _state.SetAborting();
            break;
        }
    }
}

bool DeleteProgressDialog::ShowAbortConfirmation() {
    if (_state.IsAborting()) return true;
    AbortConfirmDialog dlg;
    return dlg.Ask();
}

LONG_PTR DeleteProgressDialog::DlgProc(int msg, int param1, LONG_PTR param2) {
    if (msg == DN_ENTERIDLE) {
        if (_state.finished) {
            if (!_finished) {
                _finished = true;
                Close();
            }
        } else {
            UpdateDialog();
        }
    } else if (msg == DM_KEY && param2 == 0x1b) {
        if (ShowAbortConfirmation()) {
            _state.SetAborting();
            Close();
        }
        return TRUE;
    }
    return BaseDialog::DlgProc(msg, param1, param2);
}

void DeleteProgressDialog::UpdateDialog() {
    std::wstring current_file;
    {
        std::lock_guard<std::mutex> locker(_state.mtx_strings);
        current_file = _state.current_file;
    }
    if (current_file == _last_filename) return;
    _last_filename = current_file;
    TextToDialogControl(_i_filename, AbbreviatePathLeft(current_file, 52));
}

DeleteOperation::DeleteOperation()
    : _state(std::make_shared<ProgressState>()) {
    _state->Reset();
}

void DeleteOperation::Run(WorkFunc work_func) {
    RunModalProgress(_state, std::move(work_func), [this]() {
        DeleteProgressDialog dlg(*_state);
        dlg.Show();
    });
}

bool ADBDialogs::AskCopyMove(bool is_move, bool is_upload, std::string& destination,
                             const std::string& source_name, int item_count) {
    // Match native far2l ShellCopy: title "Copy" / "Rename/Move",
    // prompt is the verb ("Copy" / "Rename or move") + name or
    // "%d items" + " to:" (Msg::CopyFile / MoveFile, CopyFiles /
    // MoveFiles, CMLTargetTO).
    const wchar_t* title = is_move ? L"Rename/Move" : L"Copy";
    std::wstring prompt;
    std::string default_path = destination;

    const std::wstring verb = is_move ? L"Rename or move" : L"Copy";
    if (item_count > 1) {
        prompt = verb + L" " + std::to_wstring(item_count) + L" item"
               + (item_count == 1 ? L"" : L"s") + L" to:";
    } else if (!source_name.empty()) {
        prompt = verb + L" " + StrMB2Wide(source_name) + L" to:";
    } else {
        prompt = verb + L" to:";
    }

    std::string other_panel_path;
    if (!is_upload) {
        int size = g_Info.Control(PANEL_PASSIVE, FCTL_GETPANELDIR, 0, (LONG_PTR)0);
        if (size > 0) {
            wchar_t* buffer = new wchar_t[size];
            int result = g_Info.Control(PANEL_PASSIVE, FCTL_GETPANELDIR, size, (LONG_PTR)buffer);
            other_panel_path = result ? StrWide2MB(buffer) : default_path;
            delete[] buffer;
        } else {
            other_panel_path = default_path;
        }
    } else {
        other_panel_path = default_path;
    }

    return AskInput(title, prompt.c_str(), L"ADB_CopyMove", destination,
                    other_panel_path.empty() ? default_path : other_panel_path);
}

bool ADBDialogs::AskCreateDirectory(std::string& dir_name) {
    if (!AskInput(L"Create directory", L"Enter name of directory to create:",
                  L"ADB_MakeDir", dir_name, dir_name)) {
        return false;
    }
    auto ltrim = dir_name.find_first_not_of(" \t\r\n");
    if (ltrim == std::string::npos) return false;
    auto rtrim = dir_name.find_last_not_of(" \t\r\n");
    dir_name = dir_name.substr(ltrim, rtrim - ltrim + 1);
    if (dir_name == "." || dir_name == "..") return false;
    return true;
}

bool ADBDialogs::AskInput(const wchar_t* title, const wchar_t* prompt,
                          const wchar_t* history_name, std::string& input,
                          const std::string& default_value) {
    wchar_t input_buffer[4096] = {0};
    if (!default_value.empty()) {
        std::wstring wval = StrMB2Wide(default_value);
        wcsncpy(input_buffer, wval.c_str(), ARRAYSIZE(input_buffer) - 1);
    }
    std::wstring src_text_wstr = default_value.empty() ? L"" : StrMB2Wide(default_value);
    const wchar_t* src_text = src_text_wstr.empty() ? nullptr : src_text_wstr.c_str();

    bool result = g_Info.InputBox(title, prompt, history_name, src_text, input_buffer,
                                  ARRAYSIZE(input_buffer) - 1, nullptr, FIB_BUTTONS | FIB_NOUSELASTHISTORY);
    if (result) input = StrWide2MB(input_buffer);
    return result && !input.empty();
}

bool ADBDialogs::AskConfirmation(const wchar_t* title, const wchar_t* message) {
    const wchar_t* msg[] = { title, message, L"OK", L"Cancel" };
    int result = g_Info.Message(g_Info.ModuleNumber, FMSG_MB_YESNO, nullptr, msg, ARRAYSIZE(msg), 0);
    return (result == 0);
}

bool ADBDialogs::AskWarning(const wchar_t* title, const wchar_t* message) {
    const wchar_t* msg[] = { title, message, L"OK", L"Cancel" };
    int result = g_Info.Message(g_Info.ModuleNumber, FMSG_WARNING | FMSG_MB_YESNO, nullptr, msg, ARRAYSIZE(msg), 0);
    return (result == 0);
}
