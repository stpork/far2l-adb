/**
 * @file FARPlugin.cpp
 * @brief Far Manager Plugin API Implementation for MTP Plugin
 * 
 * This file implements the basic Far Manager plugin interface functions
 * required for the MTP plugin to work with far2l.
 * 
 * @author MTP Plugin Team
 * @version 1.0
 */

#include "FARPlugin.h"
#include "MTPPlugin.h"

// Global plugin instance
static MTPPlugin* g_plugin = nullptr;

// Far Manager API functions
static const PluginStartupInfo* g_psi = nullptr;

// Global Info pointer (declared in MTPPlugin.h)
// These are defined in MTPPlugin.cpp to avoid duplicate symbols
extern PluginStartupInfo g_Info;

extern "C" {

SHAREDSYMBOL int WINAPI GetMinFarVersionW(void)
{
    return MAKEFARVERSION(2, 0);
}

SHAREDSYMBOL void WINAPI SetStartupInfoW(const PluginStartupInfo *Info)
{
    g_psi = Info;
    g_Info = *Info;
    // MTPPlugin doesn't have SetStartupInfo method
}

SHAREDSYMBOL void WINAPI GetPluginInfoW(PluginInfo *Info)
{
    if (!Info) {
        return;
    }
    Info->StructSize = sizeof(*Info);
    Info->Flags = PF_FULLCMDLINE;
    Info->DiskMenuStrings = nullptr;
    Info->DiskMenuStringsNumber = 0;
    static const wchar_t *s_menu_strings[] = {L"MTP Plugin"};
    static const wchar_t *s_config_strings[] = {L"MTP Plugin"};
    Info->PluginMenuStrings = s_menu_strings;
    Info->PluginMenuStringsNumber = 1;
    Info->PluginConfigStrings = s_config_strings;
    Info->PluginConfigStringsNumber = 1;
    static const wchar_t *s_command_prefix = L"mtp";
    Info->CommandPrefix = s_command_prefix;
}

SHAREDSYMBOL HANDLE WINAPI OpenPluginW(int OpenFrom, INT_PTR Item)
{
    if (!g_plugin) {
        g_plugin = new MTPPlugin();
    }
    
    // MTPPlugin doesn't have OpenPlugin method, return the plugin instance as handle
    return (HANDLE)g_plugin;
}

SHAREDSYMBOL void WINAPI ClosePluginW(HANDLE hPlugin)
{
    // MTPPlugin doesn't have ClosePlugin method
    // Plugin cleanup is handled in ExitFARW
}

SHAREDSYMBOL int WINAPI GetFindDataW(HANDLE hPlugin, PluginPanelItem **pPanelItem, int *pItemsNumber, int OpMode)
{
    if (g_plugin) {
        return g_plugin->GetFindData(pPanelItem, pItemsNumber, OpMode);
    }
    return FALSE;
}

SHAREDSYMBOL void WINAPI FreeFindDataW(HANDLE hPlugin, PluginPanelItem *PanelItem, int ItemsNumber)
{
    if (g_plugin) {
        g_plugin->FreeFindData(PanelItem, ItemsNumber);
    }
}

SHAREDSYMBOL void WINAPI GetOpenPluginInfoW(HANDLE hPlugin, OpenPluginInfo *Info)
{
    if (g_plugin) {
        g_plugin->GetOpenPluginInfo(Info);
    }
}

SHAREDSYMBOL int WINAPI ProcessKeyW(HANDLE hPlugin, int Key, unsigned int ControlState)
{
    if (g_plugin) {
        return g_plugin->ProcessKey(Key, ControlState);
    }
    return FALSE;
}

SHAREDSYMBOL int WINAPI ProcessEventW(HANDLE hPlugin, int Event, void *Param)
{
    // MTPPlugin doesn't have ProcessEvent method
    return FALSE;
}

SHAREDSYMBOL int WINAPI SetDirectoryW(HANDLE hPlugin, const wchar_t *Dir, int OpMode)
{
    if (g_plugin) {
        return g_plugin->SetDirectory(Dir, OpMode);
    }
    return FALSE;
}

SHAREDSYMBOL int WINAPI MakeDirectoryW(HANDLE hPlugin, const wchar_t **Name, int OpMode)
{
    if (g_plugin) {
        return g_plugin->MakeDirectory(Name, OpMode);
    }
    return FALSE;
}

SHAREDSYMBOL int WINAPI DeleteFilesW(HANDLE hPlugin, PluginPanelItem *PanelItem, int ItemsNumber, int OpMode)
{
    if (g_plugin) {
        return g_plugin->DeleteFiles(PanelItem, ItemsNumber, OpMode);
    }
    return FALSE;
}

SHAREDSYMBOL HANDLE WINAPI OpenFilePluginW(const wchar_t *Name, const unsigned char *Data, int DataSize, int OpMode)
{
    // MTPPlugin doesn't have OpenFilePlugin method
    return INVALID_HANDLE_VALUE;
}

SHAREDSYMBOL int WINAPI GetLinkTargetW(HANDLE hPlugin, PluginPanelItem *PanelItem, wchar_t *Target, size_t TargetSize, int OpMode)
{
    // MTPPlugin doesn't have GetLinkTarget method
    return FALSE;
}

SHAREDSYMBOL int WINAPI ExecuteW(HANDLE hPlugin, PluginPanelItem *PanelItem, int ItemsNumber, int OpMode)
{
    // MTPPlugin doesn't have Execute method
    return FALSE;
}

SHAREDSYMBOL int WINAPI ConfigureW(int ItemNumber)
{
    // MTPPlugin doesn't have Configure method
    return FALSE;
}

SHAREDSYMBOL void WINAPI ExitFARW(void)
{
    if (g_plugin) {
        delete g_plugin;
        g_plugin = nullptr;
    }
}

SHAREDSYMBOL int WINAPI MayExitFARW(void)
{
    // MTPPlugin doesn't have MayExit method
    return TRUE;
}

} // extern "C"
