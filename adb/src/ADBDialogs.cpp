#include "ADBDialogs.h"
#include "farplug-wide.h"
#include <utils.h>

// Forward declarations
extern PluginStartupInfo g_Info;
extern void DebugLog(const char* format, ...);

bool ADBDialogs::AskCopyMove(bool is_move, bool is_upload, std::string& destination)
{
    // Determine dialog title and prompt based on operation type
    const wchar_t* title;
    const wchar_t* prompt;
    
    if (is_move) {
        if (is_upload) {
            title = L"Move to device";
            prompt = L"Enter destination path on device:";
        } else {
            title = L"Move from device";
            prompt = L"Enter destination path on local system:";
        }
    } else {
        if (is_upload) {
            title = L"Copy to device";
            prompt = L"Enter destination path on device:";
        } else {
            title = L"Copy from device";
            prompt = L"Enter destination path on local system:";
        }
    }
    
    return AskInput(title, prompt, L"ADB_CopyMove", destination, destination);
}

bool ADBDialogs::AskCreateDirectory(std::string& dir_name)
{
    DebugLog("ADBDialogs::AskCreateDirectory: Starting dialog\n");
    bool result = AskInput(L"Create directory", L"Enter name of directory to create:", 
                          L"ADB_MakeDir", dir_name, dir_name);
    DebugLog("ADBDialogs::AskCreateDirectory: Dialog result = %s\n", result ? "true" : "false");
    return result;
}

bool ADBDialogs::AskInput(const wchar_t* title, const wchar_t* prompt, 
                         const wchar_t* history_name, std::string& input, 
                         const std::string& default_value)
{
    DebugLog("ADBDialogs::AskInput: Starting InputBox\n");
    DebugLog("ADBDialogs::AskInput: title='%ls', prompt='%ls'\n", title, prompt);
    
    // Use the new variadic template InputBox function
    if (!InputBox(FIB_BUTTONS | FIB_NOUSELASTHISTORY, title, prompt, history_name, input, default_value)) {
        DebugLog("ADBDialogs::AskInput: InputBox returned false (user cancelled)\n");
        return false; // User cancelled
    }
    
    DebugLog("ADBDialogs::AskInput: InputBox returned true, input='%s'\n", input.c_str());
    
    // Check if user entered anything
    if (input.empty()) {
        DebugLog("ADBDialogs::AskInput: Empty input\n");
        return false; // Empty input
    }
    
    DebugLog("ADBDialogs::AskInput: Success, returning true\n");
    return true;
}

bool ADBDialogs::AskConfirmation(const wchar_t* title, const wchar_t* message)
{
    int result = Message(FMSG_MB_YESNO, title, message, L"OK", L"Cancel");
    return (result == 0); // 0 = Yes, 1 = No
}

bool ADBDialogs::AskWarning(const wchar_t* title, const wchar_t* message)
{
    int result = Message(FMSG_WARNING | FMSG_MB_YESNO, title, message, L"OK", L"Cancel");
    return (result == 0); // 0 = Yes, 1 = No
}