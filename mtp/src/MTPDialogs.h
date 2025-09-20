#pragma once

#include <string>
#include <vector>
#include "farplug-wide.h"
#include <utils.h>

extern PluginStartupInfo g_Info;

class MTPDialogs
{
public:
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
    
    // Message dialog with variadic arguments
    template<typename... Args>
    static int Message(unsigned int flags, Args&&... extra_lines) {
        std::vector<std::wstring> storage{ std::wstring(extra_lines)... };
        std::vector<const wchar_t*> ptrs;
        ptrs.reserve(storage.size());
        for (auto& s : storage)
            ptrs.push_back(s.c_str());

        return g_Info.Message(g_Info.ModuleNumber, flags, nullptr, ptrs.data(), (int)ptrs.size(), 0);
    }
    
    // InputBox with variadic arguments
    template<typename... Args>
    static bool InputBox(unsigned int flags, const wchar_t* title, const wchar_t* prompt, 
                        const wchar_t* history_name, std::string& input,
                        const std::string& default_value, Args&&... extra_lines) {
        wchar_t input_buffer[1024] = {0};
        if (!default_value.empty()) {
            wcscpy(input_buffer, StrMB2Wide(default_value).c_str());
        }

        std::wstring src_text_wstr = default_value.empty() ? L"" : StrMB2Wide(default_value);
        const wchar_t* src_text = src_text_wstr.empty() ? L"" : src_text_wstr.c_str();
        bool result = g_Info.InputBox(title, prompt, history_name, src_text, input_buffer, 
                                     ARRAYSIZE(input_buffer) - 1, nullptr, flags);
        
        if (result) {
            input = StrWide2MB(input_buffer);
        }
        
        return result;
    }
    
    // Overload for InputBox without additional lines
    static bool InputBox(unsigned int flags, const wchar_t* title, const wchar_t* prompt, 
                        const wchar_t* history_name, std::string& input,
                        const std::string& default_value) {
        wchar_t input_buffer[1024] = {0};
        if (!default_value.empty()) {
            wcscpy(input_buffer, StrMB2Wide(default_value).c_str());
        }
        
        std::wstring src_text_wstr = default_value.empty() ? L"" : StrMB2Wide(default_value);
        const wchar_t* src_text = src_text_wstr.empty() ? nullptr : src_text_wstr.c_str();
        bool result = g_Info.InputBox(title, prompt, history_name, src_text, input_buffer, 
                                     ARRAYSIZE(input_buffer) - 1, nullptr, flags);
        
        if (result) {
            input = StrWide2MB(input_buffer);
        }
        
        return result;
    }
    
    // Progress dialog for file transfers
    static bool ShowProgressDialog(const wchar_t* title, const wchar_t* message, 
                                   int current, int total, bool& cancelled);
    
    // Transfer confirmation dialog
    static bool AskTransferConfirmation(const wchar_t* operation, const wchar_t* source, 
                                        const wchar_t* destination, int fileCount);
};
