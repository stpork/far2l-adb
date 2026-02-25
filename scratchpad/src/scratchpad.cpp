#include <windows.h>
#include <vector>
#include <string>
#include <memory>
#include <cwchar>
#include <algorithm>
#include <fstream>
#include <farplug-wide.h>
#include <farkeys.h>
#include <WideMB.h>

#define SYSID_SCRATCHPAD 0x53637274

enum DialogItems {
    DI_BOX = 0,
    DI_MEMO,
};

static const int DLG_WIDTH = 76;   // Window width
static const int DLG_HEIGHT = 20;  // Window height

PluginStartupInfo g_far;
FarStandardFunctions g_fsf;
static std::wstring g_currentFile;  // Current scratchpad file path
static bool g_dialogActive = false;

SHAREDSYMBOL void WINAPI SetStartupInfoW(const struct PluginStartupInfo *Info)
{
    g_far = *Info;
    g_fsf = *Info->FSF;

    // Register Ctrl+S to open scratchpad via macro
    // The macro calls plugin with SysID
    ActlKeyMacro km = {};
    km.Command = MCMD_POSTMACROSTRING;
    km.Param.PlainText.SequenceText = L"plugin.Call(0x53637274, 1)";
    km.Param.PlainText.Flags = 0;
    km.Param.PlainText.AKey = KEY_CTRLS;
    g_far.AdvControl(g_far.ModuleNumber, ACTL_KEYMACRO, &km, NULL);
}

SHAREDSYMBOL void WINAPI GetPluginInfoW(struct PluginInfo *Info)
{
    Info->StructSize = sizeof(struct PluginInfo);
    Info->Flags = PF_EDITOR | PF_VIEWER | PF_DIALOG;

    static const wchar_t *menu_strings[1];
    menu_strings[0] = L"Scratchpad";

    Info->PluginConfigStrings = menu_strings;
    Info->PluginConfigStringsNumber = 1;
    Info->PluginMenuStrings = menu_strings;
    Info->PluginMenuStringsNumber = 1;

    Info->CommandPrefix = L"scratchpad";
    Info->SysID = SYSID_SCRATCHPAD;
}

static std::wstring GetScratchpadDir()
{
    const char* home = getenv("HOME");
    if (home) {
        std::string h = home;
        std::wstring wh(h.begin(), h.end());
        std::wstring dir = wh + L"/.config/far2l/plugins/scratchpad";

        WINPORT(CreateDirectory)((wh + L"/.config").c_str(), NULL);
        WINPORT(CreateDirectory)((wh + L"/.config/far2l").c_str(), NULL);
        WINPORT(CreateDirectory)((wh + L"/.config/far2l/plugins").c_str(), NULL);
        WINPORT(CreateDirectory)(dir.c_str(), NULL);

        return dir;
    }

    std::wstring modulePath = g_far.ModuleName;
    size_t lastSlash = modulePath.find_last_of(L"/\\");
    if (lastSlash != std::wstring::npos) {
        std::wstring dir = modulePath.substr(0, lastSlash) + L"/scratchpads";
        WINPORT(CreateDirectory)(dir.c_str(), NULL);
        return dir;
    }
    return L"scratchpads";
}

static std::wstring GetConfigPath()
{
    return GetScratchpadDir() + L"/last_scratchpad.txt";
}

static std::wstring GetLastScratchpad()
{
    std::wstring configPath = GetConfigPath();
    std::wifstream f(Wide2MB(configPath.c_str()));
    std::wstring last;
    if (std::getline(f, last)) {
        return last;
    }
    return L"scratchpad-001.txt";
}

static void SaveLastScratchpad(const std::wstring& name)
{
    std::wstring configPath = GetConfigPath();
    std::wofstream f(Wide2MB(configPath.c_str()));
    f << name;
}

// Load content from file to string
static std::wstring LoadFileContent(const std::wstring& path)
{
    std::wstring content;
    std::string mbPath = Wide2MB(path.c_str());
    FILE* f = fopen(mbPath.c_str(), "r");
    if (f) {
        char buf[4096];
        std::string mbContent;
        while (fgets(buf, sizeof(buf), f)) {
            mbContent += buf;
        }
        fclose(f);
        // Convert to wide string
        if (!mbContent.empty()) {
            int len = mbstowcs(NULL, mbContent.c_str(), 0);
            if (len > 0) {
                wchar_t* wbuf = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
                if (wbuf) {
                    mbstowcs(wbuf, mbContent.c_str(), len + 1);
                    content = wbuf;
                    free(wbuf);
                }
            }
        }
    }
    return content;
}

// Save content from string to file
static bool SaveFileContent(const std::wstring& path, const std::wstring& content)
{
    std::string mbPath = Wide2MB(path.c_str());
    FILE* f = fopen(mbPath.c_str(), "w");
    if (!f) {
        return false;
    }
    // Convert to multibyte
    std::string mbContent;
    if (!content.empty()) {
        int len = wcstombs(NULL, content.c_str(), 0);
        if (len > 0) {
            char* buf = (char*)malloc(len + 1);
            if (buf) {
                wcstombs(buf, content.c_str(), len + 1);
                mbContent = buf;
                free(buf);
            }
        }
    }
    fputs(mbContent.c_str(), f);
    fclose(f);
    return true;
}

// Dialog procedure - handles ESC for close/autosave and Ctrl+S for save+close
static LONG_PTR WINAPI ScratchpadDlgProc(HANDLE hDlg, int Msg, int Param1, LONG_PTR Param2)
{
    switch (Msg) {
        case DN_KEY:
            // Handle keyboard events
            if (Param1 == DI_MEMO) {
                int key = (int)Param2;

                // ESC - autosave and close
                if (key == KEY_ESC) {
                    // Get text and save
                    size_t len = g_far.SendDlgMessage(hDlg, DM_GETTEXTLENGTH, DI_MEMO, 0);
                    if (len > 0) {
                        wchar_t* buf = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
                        if (buf) {
                            g_far.SendDlgMessage(hDlg, DM_GETTEXTPTR, DI_MEMO, (LONG_PTR)buf);
                            buf[len] = 0;
                            SaveFileContent(g_currentFile, buf);
                            free(buf);
                        }
                    } else {
                        // Empty content - save empty file
                        SaveFileContent(g_currentFile, L"");
                    }
                    // Close dialog
                    g_far.SendDlgMessage(hDlg, DM_CLOSE, 0, 0);
                    return TRUE;
                }

                // Ctrl+S - save and close (same as ESC for toggle behavior)
                if (key == KEY_CTRLS) {
                    size_t len = g_far.SendDlgMessage(hDlg, DM_GETTEXTLENGTH, DI_MEMO, 0);
                    if (len > 0) {
                        wchar_t* buf = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
                        if (buf) {
                            g_far.SendDlgMessage(hDlg, DM_GETTEXTPTR, DI_MEMO, (LONG_PTR)buf);
                            buf[len] = 0;
                            SaveFileContent(g_currentFile, buf);
                            free(buf);
                        }
                    } else {
                        SaveFileContent(g_currentFile, L"");
                    }
                    g_far.SendDlgMessage(hDlg, DM_CLOSE, 0, 0);
                    return TRUE;
                }
            }
            break;

        case DN_CLOSE:
            // Autosave on any close (including mouse click outside)
            {
                size_t len = g_far.SendDlgMessage(hDlg, DM_GETTEXTLENGTH, DI_MEMO, 0);
                if (len > 0) {
                    wchar_t* buf = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
                    if (buf) {
                        g_far.SendDlgMessage(hDlg, DM_GETTEXTPTR, DI_MEMO, (LONG_PTR)buf);
                        buf[len] = 0;
                        SaveFileContent(g_currentFile, buf);
                        free(buf);
                    }
                } else {
                    SaveFileContent(g_currentFile, L"");
                }
                g_dialogActive = false;
            }
            break;
    }

    return g_far.DefDlgProc(hDlg, Msg, Param1, Param2);
}

// Open scratchpad in a dialog window
static void OpenScratchpadDialog(std::wstring name)
{
    if (name.empty()) {
        name = GetLastScratchpad();
    }

    g_currentFile = GetScratchpadDir() + L"/" + name;

    // Ensure file exists
    if (WINPORT(GetFileAttributes)(g_currentFile.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // Create empty file
        SaveFileContent(g_currentFile, L"");
    }

    // Load content
    std::wstring content = LoadFileContent(g_currentFile);

    // Use fixed centered position (-1 for auto-centering)
    int x1 = -1;
    int y1 = -1;
    int x2 = x1 + DLG_WIDTH - 1;
    int y2 = y1 + DLG_HEIGHT - 1;

    // Create dialog items
    std::wstring title = L" Scratchpad: " + name + L" (Esc/Ctrl+S: Save&Close) ";

    FarDialogItem items[2] = {};

    // Box
    items[DI_BOX].Type = DI_DOUBLEBOX;
    items[DI_BOX].X1 = 3;
    items[DI_BOX].Y1 = 1;
    items[DI_BOX].X2 = DLG_WIDTH - 4;
    items[DI_BOX].Y2 = DLG_HEIGHT - 2;
    items[DI_BOX].PtrData = title.c_str();

    // Memo edit
    items[DI_MEMO].Type = DI_MEMOEDIT;
    items[DI_MEMO].X1 = 5;
    items[DI_MEMO].Y1 = 2;
    items[DI_MEMO].X2 = DLG_WIDTH - 6;
    items[DI_MEMO].Y2 = DLG_HEIGHT - 3;
    items[DI_MEMO].Focus = 1;
    items[DI_MEMO].PtrData = content.c_str();

    // Initialize and run dialog
    HANDLE hDlg = g_far.DialogInit(
        g_far.ModuleNumber,
        x1, y1, x2, y2,
        NULL,  // help topic
        items,
        2,
        0,
        0,
        ScratchpadDlgProc,
        0
    );

    if (hDlg != INVALID_HANDLE_VALUE) {
        g_dialogActive = true;
        SaveLastScratchpad(name);
        g_far.DialogRun(hDlg);
        g_far.DialogFree(hDlg);
    }
}

static void CreateNewScratchpad()
{
    std::wstring dir = GetScratchpadDir();
    for (int i = 1; i < 1000; ++i) {
        wchar_t name[64];
        swprintf(name, 64, L"scratchpad-%03d.txt", i);
        std::wstring fullPath = dir + L"/" + name;
        if (WINPORT(GetFileAttributes)(fullPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            OpenScratchpadDialog(name);
            return;
        }
    }
}

static void DeleteScratchpad(const std::wstring& name)
{
    std::wstring fullPath = GetScratchpadDir() + L"/" + name;

    const wchar_t *MsgItems[] = { L"Delete Scratchpad", L"Do you want to delete scratchpad?", name.c_str(), L"Delete", L"Cancel" };
    if (g_far.Message(g_far.ModuleNumber, FMSG_WARNING, NULL, MsgItems, 5, 2) != 0)
        return;

    WINPORT(DeleteFile)(fullPath.c_str());
}

static void RenameScratchpad(const std::wstring& oldName)
{
    wchar_t newName[MAX_PATH];
    wcscpy(newName, oldName.c_str());

    if (g_far.InputBox(L"Rename Scratchpad", L"Enter new name:", L"ScratchpadRename", oldName.c_str(), newName, MAX_PATH, NULL, FIB_NONE)) {
        std::wstring dir = GetScratchpadDir();
        std::wstring oldPath = dir + L"/" + oldName;
        std::wstring newPath = dir + L"/" + newName;

        WINPORT(MoveFile)(oldPath.c_str(), newPath.c_str());
    }
}

static void SelectionMenu()
{
    while (true) {
        std::wstring dir = GetScratchpadDir();
        std::vector<std::wstring> files;

        WIN32_FIND_DATAW fd;
        HANDLE hFind = WINPORT(FindFirstFile)((dir + L"/*.txt").c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                files.push_back(fd.cFileName);
            } while (WINPORT(FindNextFile)(hFind, &fd));
            WINPORT(FindClose)(hFind);
        }

        if (files.empty()) {
            CreateNewScratchpad();
            return;
        }

        std::sort(files.begin(), files.end());

        std::vector<FarMenuItem> menuItems;
        std::wstring last = GetLastScratchpad();

        for (size_t i = 0; i < files.size(); ++i) {
            FarMenuItem item = {};
            item.Text = wcsdup(files[i].c_str());
            if (files[i] == last) {
                item.Selected = TRUE;
            }
            menuItems.push_back(item);
        }

        FarMenuItem sep = {}; sep.Separator = TRUE; menuItems.push_back(sep);
        FarMenuItem newItem = {}; newItem.Text = L"Create new scratchpad..."; menuItems.push_back(newItem);

        int BreakKeys[] = { KEY_INS, KEY_DEL, KEY_NUMPAD0, KEY_F4, KEY_F2, 0 };
        int BreakCode = -1;

        intptr_t res = g_far.Menu(g_far.ModuleNumber, -1, -1, 0, FMENU_WRAPMODE | FMENU_AUTOHIGHLIGHT,
                                 L"Scratchpads", L"Enter: Open, Ins: New, Del: Delete, F2: Rename", L"Scratchpad",
                                 BreakKeys, &BreakCode, menuItems.data(), (int)menuItems.size());

        for (auto& item : menuItems) if (!item.Separator) free((void*)item.Text);

        if (res >= 0 && BreakCode == -1) {
            if (res < (intptr_t)files.size()) {
                OpenScratchpadDialog(files[res]);
                return;
            } else if (res == (intptr_t)files.size() + 1) {
                CreateNewScratchpad();
                return;
            }
        } else if (BreakCode != -1) {
            if (res >= 0 && res < (intptr_t)files.size()) {
                if (BreakKeys[BreakCode] == KEY_DEL) {
                    DeleteScratchpad(files[res]);
                } else if (BreakKeys[BreakCode] == KEY_F2) {
                    RenameScratchpad(files[res]);
                } else if (BreakKeys[BreakCode] == KEY_INS || BreakKeys[BreakCode] == KEY_NUMPAD0) {
                    CreateNewScratchpad();
                    return;
                }
            } else if (BreakKeys[BreakCode] == KEY_INS || BreakKeys[BreakCode] == KEY_NUMPAD0) {
                CreateNewScratchpad();
                return;
            }
        } else {
            return;
        }
    }
}

SHAREDSYMBOL int WINAPI ProcessEditorEventW(int Event, void *Param)
{
    // No longer needed - using dialog-based approach
    return 0;
}

SHAREDSYMBOL HANDLE WINAPI OpenPluginW(int OpenFrom, INT_PTR Item)
{
    if (OpenFrom & OPEN_FROMMACRO) {
        if (Item == 1) {
            OpenScratchpadDialog(L"");
        } else if (Item == 2) {
            CreateNewScratchpad();
        }
    } else {
        SelectionMenu();
    }

    return INVALID_HANDLE_VALUE;
}

SHAREDSYMBOL void WINAPI ClosePluginW(HANDLE hPlugin) {}
