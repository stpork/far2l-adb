#include "ADBDialogs.h"
#include "farplug-wide.h"
#include <utils.h>

extern PluginStartupInfo g_Info;
extern void DebugLog(const char* format, ...);

bool ADBDialogs::AskCopyMove(bool is_move, bool is_upload, std::string& destination)
{
    if (is_upload) {
        return true;
    }
    
    const wchar_t* title;
    const wchar_t* prompt;
    
    if (is_move) {
        title = L"Move from device";
        prompt = L"Enter destination path on local system:";
    } else {
        title = L"Copy from device";
        prompt = L"Enter destination path on local system:";
    }
    
    std::string other_panel_path;
    
    PanelInfo panelInfo = {};
    g_Info.Control(PANEL_PASSIVE, FCTL_GETPANELINFO, 0, (LONG_PTR)&panelInfo);
    
    int size = g_Info.Control(PANEL_PASSIVE, FCTL_GETPANELDIR, 0, (LONG_PTR)0);
    
    if (size > 0) {
        wchar_t* buffer = new wchar_t[size];
        int result = g_Info.Control(PANEL_PASSIVE, FCTL_GETPANELDIR, size, (LONG_PTR)buffer);
        if (result) {
            other_panel_path = StrWide2MB(buffer);
        } else {
            other_panel_path = destination;
        }
        delete[] buffer;
    } else {
        other_panel_path = destination;
    }
    
    return AskInput(title, prompt, L"ADB_CopyMove", destination, other_panel_path);
}

bool ADBDialogs::AskCreateDirectory(std::string& dir_name)
{
    return AskInput(L"Create directory", L"Enter name of directory to create:", 
                    L"ADB_MakeDir", dir_name, dir_name);
}

bool ADBDialogs::AskInput(const wchar_t* title, const wchar_t* prompt, 
                         const wchar_t* history_name, std::string& input, 
                         const std::string& default_value)
{
    if (!InputBox(FIB_BUTTONS | FIB_NOUSELASTHISTORY, title, prompt, history_name, input, default_value)) {
        return false;
    }
    
    if (input.empty()) {
        return false;
    }
    
    return true;
}

bool ADBDialogs::AskConfirmation(const wchar_t* title, const wchar_t* message)
{
    int result = Message(FMSG_MB_YESNO, title, message, L"OK", L"Cancel");
    return (result == 0);
}

bool ADBDialogs::AskWarning(const wchar_t* title, const wchar_t* message)
{
    int result = Message(FMSG_WARNING | FMSG_MB_YESNO, title, message, L"OK", L"Cancel");
    return (result == 0);
}