// FAR Manager includes
#include "farplug-wide.h"

// Standard library includes
#include <algorithm>

// Local includes
#include "ADBPlugin.h"
#include "FARPlugin.h"

// Debug logging function declaration
extern void DebugLog(const char* format, ...);

static ADBPlugin *impl = nullptr;

extern "C" {

SHAREDSYMBOL int WINAPI GetMinFarVersionW()
{
	return MAKEFARVERSION(2, 0);
}

SHAREDSYMBOL void WINAPI SetStartupInfoW(const PluginStartupInfo *Info)
{
	// Copy the Info into global instance (like NetRocks)
	if (Info) {
		memcpy(&g_Info, Info, std::min((size_t)Info->StructSize, sizeof(PluginStartupInfo)));
	}
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
	static const wchar_t *s_menu_strings[] = {L"ADB Plugin"};
	static const wchar_t *s_config_strings[] = {L"ADB Plugin"};
	Info->PluginMenuStrings = s_menu_strings;
	Info->PluginMenuStringsNumber = 1;
	Info->PluginConfigStrings = s_config_strings;
	Info->PluginConfigStringsNumber = 1;
	Info->CommandPrefix = nullptr;  // No prefix required
}

SHAREDSYMBOL HANDLE WINAPI OpenPluginW(int OpenFrom, INT_PTR Item)
{
	try {
		// Create new plugin instance
		impl = new ADBPlugin();
		return (HANDLE)impl;
	} catch (...) {
		return INVALID_HANDLE_VALUE;
	}
}

SHAREDSYMBOL void WINAPI ClosePluginW(HANDLE hPlugin)
{
	if (hPlugin && hPlugin != INVALID_HANDLE_VALUE) {
		ADBPlugin *plugin = (ADBPlugin*)hPlugin;
		delete plugin;
		impl = nullptr;
	}
}

SHAREDSYMBOL int WINAPI GetFindDataW(HANDLE hPlugin, PluginPanelItem **pPanelItem, int *pItemsNumber, int OpMode)
{
	if (!hPlugin || hPlugin == INVALID_HANDLE_VALUE) {
		return 0;
	}
	ADBPlugin *plugin = (ADBPlugin*)hPlugin;
	return plugin->GetFindData(pPanelItem, pItemsNumber, OpMode);
}

SHAREDSYMBOL void WINAPI FreeFindDataW(HANDLE hPlugin, PluginPanelItem *PanelItem, int ItemsNumber)
{
	if (hPlugin && hPlugin != INVALID_HANDLE_VALUE) {
		ADBPlugin *plugin = (ADBPlugin*)hPlugin;
		plugin->FreeFindData(PanelItem, ItemsNumber);
	}
}

SHAREDSYMBOL void WINAPI GetOpenPluginInfoW(HANDLE hPlugin, OpenPluginInfo *Info)
{
	if (!hPlugin || hPlugin == INVALID_HANDLE_VALUE) {
		return;
	}
	ADBPlugin *plugin = (ADBPlugin*)hPlugin;
	plugin->GetOpenPluginInfo(Info);
}

SHAREDSYMBOL int WINAPI ProcessKeyW(HANDLE hPlugin, int Key, unsigned int ControlState)
{
	if (!hPlugin || hPlugin == INVALID_HANDLE_VALUE) {
		return 0;
	}
	ADBPlugin *plugin = (ADBPlugin*)hPlugin;
	return plugin->ProcessKey(Key, ControlState);
}

SHAREDSYMBOL int WINAPI ProcessEventW(HANDLE hPlugin, int Event, void *Param)
{
	// Command processing disabled for now
	return 0;
}

SHAREDSYMBOL int WINAPI SetDirectoryW(HANDLE hPlugin, const wchar_t *Dir, int OpMode)
{
	if (!hPlugin || hPlugin == INVALID_HANDLE_VALUE) {
		return 0;
	}
	ADBPlugin *plugin = (ADBPlugin*)hPlugin;
	return plugin->SetDirectory(Dir, OpMode);
}

SHAREDSYMBOL int WINAPI MakeDirectoryW(HANDLE hPlugin, const wchar_t **Name, int OpMode)
{
	if (!hPlugin || hPlugin == INVALID_HANDLE_VALUE) {
		return 0;
	}
	
	ADBPlugin *plugin = (ADBPlugin*)hPlugin;
	return plugin->MakeDirectory(Name, OpMode);
}

SHAREDSYMBOL int WINAPI DeleteFilesW(HANDLE hPlugin, PluginPanelItem *PanelItem, int ItemsNumber, int OpMode)
{
	if (!hPlugin) {
		return FALSE;
	}
	
	ADBPlugin *plugin = static_cast<ADBPlugin*>(hPlugin);
	return plugin->DeleteFiles(PanelItem, ItemsNumber, OpMode);
}

SHAREDSYMBOL int WINAPI GetFilesW(HANDLE hPlugin, PluginPanelItem *PanelItem, int ItemsNumber, int Move, const wchar_t **DestPath, int OpMode)
{
	if (!hPlugin || hPlugin == INVALID_HANDLE_VALUE) {
		return 0;
	}
	ADBPlugin *plugin = (ADBPlugin*)hPlugin;
	int result = plugin->GetFiles(PanelItem, ItemsNumber, Move, DestPath, OpMode);
	return result;
}

SHAREDSYMBOL HANDLE WINAPI OpenFilePluginW(const wchar_t *Name, const unsigned char *Data, int DataSize, int OpMode)
{
	return INVALID_HANDLE_VALUE;
}

SHAREDSYMBOL int WINAPI PutFilesW(HANDLE hPlugin, PluginPanelItem *PanelItem, int ItemsNumber, int Move, const wchar_t *SrcPath, int OpMode)
{
	if (!hPlugin || hPlugin == INVALID_HANDLE_VALUE) {
		return 0;
	}
	ADBPlugin *plugin = (ADBPlugin*)hPlugin;
	return plugin->PutFiles(PanelItem, ItemsNumber, Move, SrcPath, OpMode);
}

SHAREDSYMBOL int WINAPI ProcessHostFileW(HANDLE hPlugin, PluginPanelItem *PanelItem, int ItemsNumber, int OpMode)
{
	if (!hPlugin || hPlugin == INVALID_HANDLE_VALUE) {
		return 0;
	}
	ADBPlugin *plugin = (ADBPlugin*)hPlugin;
	return plugin->ProcessHostFile(PanelItem, ItemsNumber, OpMode);
}

SHAREDSYMBOL int WINAPI GetLinkTargetW(HANDLE hPlugin, PluginPanelItem *PanelItem, wchar_t *Target, size_t TargetSize, int OpMode)
{
	return 0;
}

SHAREDSYMBOL int WINAPI ExecuteW(HANDLE hPlugin, PluginPanelItem *PanelItem, int ItemsNumber, int OpMode)
{
	return 0;
}

SHAREDSYMBOL int WINAPI ConfigureW(int ItemNumber)
{
	return 0;
}

SHAREDSYMBOL void WINAPI ExitFARW()
{
}

SHAREDSYMBOL int WINAPI MayExitFARW()
{
	return 1;
}

} // extern "C"