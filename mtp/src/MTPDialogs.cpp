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
    if (total > 0) {
        int percent = (current * 100) / total;
        
        // Create progress bar like far2l does
        const int BAR_WIDTH = 50;
        std::wstring progressBar;
        progressBar.reserve(BAR_WIDTH + 10);
        
        int filledLength = (percent * BAR_WIDTH) / 100;
        for (int i = 0; i < BAR_WIDTH; i++) {
            if (i < filledLength) {
                progressBar += L'\x2588'; // Full block
            } else {
                progressBar += L'\x2591'; // Light shade
            }
        }
        
        std::wstring progressLine = L"Progress: " + std::to_wstring(current) + L"/" + std::to_wstring(total) + 
                                   L" (" + std::to_wstring(percent) + L"%)";
        std::wstring barLine = L"[" + progressBar + L"]";

        // Show progress dialog that auto-dismisses - use FMSG_KEEPBACKGROUND
        // This creates a non-blocking progress dialog that updates in real-time
        const wchar_t* msgLines[] = { message, progressLine.c_str(), barLine.c_str() };
        g_Info.Message(g_Info.ModuleNumber, FMSG_KEEPBACKGROUND, nullptr, msgLines, 3, 0);
    } else {
        // Single line message
        const wchar_t* msgLines[] = { message };
        g_Info.Message(g_Info.ModuleNumber, FMSG_KEEPBACKGROUND, nullptr, msgLines, 1, 0);
    }
    
    // For now, we don't implement cancellation
    cancelled = false;
    return true;
}

bool MTPDialogs::AskTransferConfirmation(const wchar_t* operation, const wchar_t* source, 
                                        const wchar_t* destination, int fileCount)
{
    std::wstring operationLine = L"Operation: " + std::wstring(operation);
    std::wstring sourceLine = L"Source: " + std::wstring(source);
    std::wstring destLine = L"Destination: " + std::wstring(destination);
    std::wstring filesLine = L"Files: " + std::to_wstring(fileCount);
    
    // Use variadic template for multi-line dialog
    int result = Message(FMSG_MB_YESNO, L"Confirm Transfer", 
                        operationLine.c_str(), 
                        sourceLine.c_str(), 
                        destLine.c_str(), 
                        filesLine.c_str(), 
                        L"", 
                        L"Do you want to continue?");
    return (result == 0);
}

// MTPProgressDialog implementation (simple but effective)
MTPDialogs::MTPProgressDialog::MTPProgressDialog(const std::string& operation, const std::string& fileName, int total)
    : _operation(operation), _fileName(fileName), _total(total), _finished(false), _cancelled(false)
{
}

void MTPDialogs::MTPProgressDialog::Show()
{
    // Initial progress display
    UpdateProgress(0);
}

void MTPDialogs::MTPProgressDialog::UpdateProgress(int current, const std::string& currentFile)
{
    if (_finished) return;
    
    // Check for cancellation (ESC key or Cancel button)
    if (CheckForCancellation()) {
        _cancelled = true;
        return;
    }
    
    int percent = (_total > 0) ? (current * 100) / _total : 0;
    
    std::string displayFile = currentFile.empty() ? _fileName : currentFile;
    std::wstring wDisplayFile = StrMB2Wide(displayFile);
    std::wstring wOperation = StrMB2Wide(_operation);
    
    // Create progress message like the image shows
    std::wstring progressMsg = wOperation + L" the file";
    
    // Create progress bar using far2l's BoxSymbols
    const int BAR_WIDTH = 50;
    std::wstring progressBar;
    progressBar.reserve(BAR_WIDTH + 10);
    
    int filledLength = (percent * BAR_WIDTH) / 100;
    for (int i = 0; i < BAR_WIDTH; i++) {
        if (i < filledLength) {
            progressBar += L'\x2588'; // Full block
        } else {
            progressBar += L'\x2591'; // Light shade
        }
    }
    
    std::wstring progressLine = L"Progress: " + std::to_wstring(current) + L"/" + std::to_wstring(_total) + 
                               L" (" + std::to_wstring(percent) + L"%)";
    std::wstring barLine = L"[" + progressBar + L"]";
    
    // Show progress dialog (non-blocking) - this creates the dialog box like in your image
    const wchar_t* msgLines[] = { progressMsg.c_str(), wDisplayFile.c_str(), progressLine.c_str(), barLine.c_str() };
    g_Info.Message(g_Info.ModuleNumber, FMSG_KEEPBACKGROUND, nullptr, msgLines, 4, 0);
}

void MTPDialogs::MTPProgressDialog::SetFinished()
{
    _finished = true;
    // Clear progress dialog
    const wchar_t* msgLines[] = { L"" };
    g_Info.Message(g_Info.ModuleNumber, FMSG_KEEPBACKGROUND, nullptr, msgLines, 1, 0);
}

void MTPDialogs::MTPProgressDialog::CreateProgressUI()
{
    // Simple message-based progress
}

void MTPDialogs::MTPProgressDialog::UpdateProgressBar(int percent)
{
    // Progress bar update logic (called by UpdateProgress)
}

bool MTPDialogs::MTPProgressDialog::CheckForCancellation()
{
    // Check for ESC key press or Cancel button
    // We'll use a simple approach: show a confirmation dialog if ESC is pressed
    
    // For now, we'll implement a basic cancellation check
    // In a full implementation, we'd integrate with far2l's input system
    
    // Simple cancellation check - in practice, this would be called periodically
    // during long operations and would check for ESC key or other cancellation signals
    
    return false; // No cancellation for now - would need proper input handling
}

// Convenience method
std::unique_ptr<MTPDialogs::MTPProgressDialog> MTPDialogs::ShowProgress(const std::string& operation, const std::string& fileName, int total)
{
    auto dialog = std::make_unique<MTPProgressDialog>(operation, fileName, total);
    dialog->Show();
    return dialog;
}
