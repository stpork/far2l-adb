#pragma once

#include <string>
#include <vector>
#include "farplug-wide.h"
#include <utils.h>

// Forward declarations
extern PluginStartupInfo g_Info;

// General dialog utility class for ADB plugin
class ADBDialogs
{
public:
    // Copy/move confirmation dialog
    static bool AskCopyMove(bool is_move, bool is_upload, std::string& destination);
    
    // Create directory dialog
    static bool AskCreateDirectory(std::string& dir_name);
    
    // Generic input dialog
    static bool AskInput(const wchar_t* title, const wchar_t* prompt, 
                        const wchar_t* history_name, std::string& input, 
                        const std::string& default_value = "");
    
    // Generic confirmation dialog
    static bool AskConfirmation(const wchar_t* title, const wchar_t* message);
    
    // Generic warning dialog
    static bool AskWarning(const wchar_t* title, const wchar_t* message);
    
    // Variadic template functions for cleaner multi-line dialogs
    
    // Message dialog with variadic arguments
    template<typename... Args>
    static int Message(unsigned int flags, Args&&... extra_lines) {
        std::vector<std::wstring> storage{ std::wstring(extra_lines)... }; // owns the strings
        std::vector<const wchar_t*> ptrs;
        ptrs.reserve(storage.size());
        for (auto& s : storage)
            ptrs.push_back(s.c_str());

        return g_Info.Message(g_Info.ModuleNumber, flags, nullptr, ptrs.data(), (int)ptrs.size(), 0);
    }
    
    // InputBox with variadic arguments (title and prompt are mandatory, additional lines are optional)
    template<typename... Args>
    static bool InputBox(unsigned int flags, const wchar_t* title, const wchar_t* prompt, 
                        const wchar_t* history_name, std::string& input,
                        const std::string& default_value, Args&&... extra_lines) {
        // Create input buffer
        wchar_t input_buffer[1024] = {0};
        if (!default_value.empty()) {
            wcscpy(input_buffer, StrMB2Wide(default_value).c_str());
        }

        std::wstring src_text_wstr = default_value.empty() ? L"" : StrMB2Wide(default_value);
        const wchar_t* src_text = src_text_wstr.empty() ? L"" : src_text_wstr.c_str();
        bool result = g_Info.InputBox(title, prompt, history_name, src_text, input_buffer, 
                                     ARRAYSIZE(input_buffer) - 1, nullptr, flags);
        
        // Copy the result back to input parameter
        if (result) {
            input = StrWide2MB(input_buffer);
        }
        
        return result;
    }
    
    // Overload for InputBox without additional lines
    static bool InputBox(unsigned int flags, const wchar_t* title, const wchar_t* prompt, 
                        const wchar_t* history_name, std::string& input,
                        const std::string& default_value) {
        // Create input buffer
        wchar_t input_buffer[1024] = {0};
        if (!default_value.empty()) {
            wcscpy(input_buffer, StrMB2Wide(default_value).c_str());
        }
        
        std::wstring src_text_wstr = default_value.empty() ? L"" : StrMB2Wide(default_value);
        const wchar_t* src_text = src_text_wstr.empty() ? nullptr : src_text_wstr.c_str();
        bool result = g_Info.InputBox(title, prompt, history_name, src_text, input_buffer, 
                                     ARRAYSIZE(input_buffer) - 1, nullptr, flags);
        
        // Copy the result back to input parameter
        if (result) {
            input = StrWide2MB(input_buffer);
        }
        
        return result;
    }
};
