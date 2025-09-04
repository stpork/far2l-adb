#pragma once

// Standard library includes
#include <vector>
#include <memory>
#include <string>
#include <map>

// System includes
#include <wchar.h>

// FAR Manager includes
#include "farplug-wide.h"

// Local includes
#include "ADBDevice.h"

// Global Info pointer declaration
extern PluginStartupInfo g_Info;

class ADBPlugin
{
	friend class AllADB;

	// Panel state
	wchar_t _panel_title[64];
	wchar_t _cur_dir[1024];
	wchar_t _mk_dir[1024];
	wchar_t _format[256];
	
	// Custom panel mode for device selection (like NetRocks host selection)
	PanelMode _panel_mode;

	// Standalone config
	std::wstring _standalone_config;
	bool _allow_remember_location_dir;
	
	bool _isFileMode;  // true = device selection, false = file browsing
	bool _isConnected;               // false = device selection, true = file navigation
	std::string _deviceSerial;      // Current device serial
	std::string _devicePath;        // Current path on device
	
	// ADBDevice for file operations
	std::shared_ptr<class ADBDevice> _adbDevice;

public:
	ADBPlugin(const wchar_t *path = nullptr, bool path_is_standalone_config = false, int OpMode = 0);
	virtual ~ADBPlugin();

	// Panel operations
	int GetFindData(PluginPanelItem **pPanelItem, int *pItemsNumber, int OpMode);
	void FreeFindData(PluginPanelItem *PanelItem, int ItemsNumber);
	void GetOpenPluginInfo(OpenPluginInfo *Info);
	int SetDirectory(const wchar_t *Dir, int OpMode);
	int ProcessKey(int Key, unsigned int ControlState);
	int ProcessEventCommand(const wchar_t *cmd, HANDLE hPlugin = nullptr);
	
	// Custom column definitions
	void SetupDeviceSelectionMode();
	void SetupFileBrowsingMode();

	int ExitDeviceFilePanel();
	int GetDeviceFileData(PluginPanelItem **pPanelItem, int *pItemsNumber, int OpMode);
	
	// Device selection methods
	std::string GetCurrentPanelItemDeviceName();
	std::string GetFallbackDeviceName();
	int GetHighlightedDeviceIndex();
	
	// Device connection methods (NetRocks pattern)
	std::string GetFirstAvailableDevice();
	bool ByKey_TryEnterSelectedDevice();
	bool ConnectToDevice(const std::string &deviceSerial);
	void UpdatePathInfo();
	
	// File transfer methods
	int GetFiles(PluginPanelItem *PanelItem, int ItemsNumber, int Move, const wchar_t **DestPath, int OpMode);
	int PutFiles(PluginPanelItem *PanelItem, int ItemsNumber, int Move, const wchar_t *SrcPath, int OpMode);
	int ProcessHostFile(PluginPanelItem *PanelItem, int ItemsNumber, int OpMode);
	int DeleteFiles(PluginPanelItem *PanelItem, int ItemsNumber, int OpMode);
	int MakeDirectory(const wchar_t **Name, int OpMode);
	bool IsDirectoryEmpty(const std::string &devicePath);
	
	// far2l API access
	static PluginStartupInfo *GetInfo();
};
