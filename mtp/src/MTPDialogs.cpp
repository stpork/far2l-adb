#include "MTPDialogs.h"

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

bool MTPDialogs::ShowProgressDialog(const wchar_t* title, const wchar_t* message, 
                                   int current, int total, bool& cancelled)
{
    // Simple progress display using Far Manager's message system
    // For a more sophisticated progress dialog, we would need to implement
    // a custom dialog with progress bar, but this provides basic feedback
    
    std::wstring progressMsg = message;
    if (total > 0) {
        int percent = (current * 100) / total;
        progressMsg += L"\nProgress: " + std::to_wstring(current) + L"/" + std::to_wstring(total) + 
                      L" (" + std::to_wstring(percent) + L"%)";
    }
    
    // Show a non-blocking message
    const wchar_t* msgArray[] = { progressMsg.c_str() };
    g_Info.Message(g_Info.ModuleNumber, FMSG_MB_OK, title, msgArray, 1, 0);
    
    // For now, we don't implement cancellation
    cancelled = false;
    return true;
}

bool MTPDialogs::AskTransferConfirmation(const wchar_t* operation, const wchar_t* source, 
                                        const wchar_t* destination, int fileCount)
{
    std::wstring message = L"Operation: " + std::wstring(operation) + L"\n";
    message += L"Source: " + std::wstring(source) + L"\n";
    message += L"Destination: " + std::wstring(destination) + L"\n";
    message += L"Files: " + std::to_wstring(fileCount) + L"\n\n";
    message += L"Do you want to continue?";
    
    int result = Message(FMSG_MB_YESNO, L"Confirm Transfer", message.c_str());
    return (result == 0);
}
