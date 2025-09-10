#include "MTPDialogs.h"
#include "MTPLog.h"

extern PluginStartupInfo g_Info;

bool MTPDialogs::AskCreateDirectory(std::string& dir_name)
{
    return AskInput(L"Create directory", L"Enter name of directory to create:", 
                    L"MTP_MakeDir", dir_name, dir_name);
}

bool MTPDialogs::AskInput(const wchar_t* title, const wchar_t* prompt, 
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

bool MTPDialogs::AskConfirmation(const wchar_t* title, const wchar_t* message)
{
    int result = Message(FMSG_MB_YESNO, title, message, L"OK", L"Cancel");
    return (result == 0);
}

bool MTPDialogs::AskWarning(const wchar_t* title, const wchar_t* message)
{
    int result = Message(FMSG_WARNING | FMSG_MB_YESNO, title, message, L"OK", L"Cancel");
    return (result == 0);
}
