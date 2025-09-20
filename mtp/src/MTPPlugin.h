#pragma once

#include <string>
#include <wchar.h>
#include "farplug-wide.h"
#include <libmtp.h>
#include "MTPDevice.h"
#include "MTPFileSystem.h"

extern PluginStartupInfo g_Info;
extern FarStandardFunctions g_FSF;

class MTPPlugin
{
	friend class AllMTP;

private:
	// Panel state
	wchar_t _PanelTitle[64];
	wchar_t _mk_dir[1024];
	std::wstring _dynamicPanelTitle;
	
	// Configuration
	std::wstring _standalone_config;
	bool _allow_remember_location_dir;
	
	// Connection state
	bool _isConnected;
	std::string _deviceSerial;
	std::string _deviceName;
	uint32_t _currentStorageID;
	uint32_t _currentDirID;
	
	// Cursor position tracking
	std::string _lastEnteredDir;
	std::string _lastEnteredDirName;
	
	// MTP components
	std::shared_ptr<class MTPDevice> _mtpDevice;
	std::shared_ptr<class MTPFileSystem> _mtpFileSystem;

public:
	MTPPlugin(const wchar_t *path = nullptr, bool path_is_standalone_config = false, int OpMode = 0);
	virtual ~MTPPlugin();

	// Panel operations
	int GetFindData(PluginPanelItem **pPanelItem, int *pItemsNumber, int OpMode);
	void FreeFindData(PluginPanelItem *PanelItem, int ItemsNumber);
	void GetOpenPluginInfo(OpenPluginInfo *Info);
	int SetDirectory(const wchar_t *Dir, int OpMode);
	int ProcessKey(int Key, unsigned int ControlState);
	
	// File operations
	int MakeDirectory(const wchar_t **Name, int OpMode);
	int DeleteFiles(PluginPanelItem *PanelItem, int ItemsNumber, int OpMode);
	
	// File transfer operations
	int GetFiles(PluginPanelItem *PanelItem, int ItemsNumber, int Move, const wchar_t **DestPath, int OpMode);
	int PutFiles(PluginPanelItem *PanelItem, int ItemsNumber, int Move, const wchar_t *SrcPath, int OpMode);
	
	// Data retrieval
	int GetDeviceData(PluginPanelItem **pPanelItem, int *pItemsNumber);
	int GetFileData(PluginPanelItem **pPanelItem, int *pItemsNumber);
	
	// Device management
	bool ByKey_TryEnterSelectedDevice();
	bool ConnectToDevice(const std::string &deviceId);
	std::string GetDeviceFriendlyName(const std::string& deviceId);
	std::string GetDeviceFriendlyNameFromRawDevice(const LIBMTP_raw_device_t& rawDevice);
	std::string GetCurrentPanelItemDeviceName();
	
	// UI helpers
	std::wstring GeneratePanelTitle();
	
	// ID utilities
	static bool IsEncodedId(const std::string& str);
	std::string GetCurrentEncodedId() const;
	void SetCurrentFromEncodedId(const std::string& encodedId);
	
	// API access
	static PluginStartupInfo *GetInfo();
};
