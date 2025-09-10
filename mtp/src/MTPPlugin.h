#pragma once

// Standard library includes
#include <string>

// System includes
#include <wchar.h>

// FAR Manager includes
#include "farplug-wide.h"

// libmtp includes
#include <libmtp.h>

// Local includes
#include "MTPDevice.h"
#include "MTPFileSystem.h"

// Global Info pointer declaration
extern PluginStartupInfo g_Info;
extern FarStandardFunctions g_FSF;

class MTPPlugin
{
	friend class AllMTP;

	// Panel state
	wchar_t _PanelTitle[64];
	wchar_t _mk_dir[1024];
	std::wstring _dynamicPanelTitle;  // Dynamic panel title for GetOpenPluginInfo
	
	// Standalone config
	std::wstring _standalone_config;
	bool _allow_remember_location_dir;
	
	bool _isConnected;               // true = connected to device (file mode), false = device selection mode
	std::string _deviceSerial;      // Current device serial/identifier
	std::string _deviceName;        // Current device friendly name
	uint32_t _currentStorageID;     // Current storage ID (0 = no storage selected)
	uint32_t _currentDirID;         // Current directory ID (0 = at storage root)
	
	// Cursor position tracking for better UX
	std::string _lastEnteredDir;
	std::string _lastEnteredDirName;  // Name of the directory we entered (for ".." navigation)
	
	// MTPDevice for file operations
	std::shared_ptr<class MTPDevice> _mtpDevice;
	
	// MTPFileSystem for file system operations
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
	
	int ExitDeviceFilePanel();
	int GetDeviceData(PluginPanelItem **pPanelItem, int *pItemsNumber);
	int GetFileData(PluginPanelItem **pPanelItem, int *pItemsNumber);
	
    // Device selection methods
    bool ByKey_TryEnterSelectedDevice();
    std::string GetDeviceFriendlyName(const std::string& deviceId);
	std::string GetDeviceFriendlyNameFromRawDevice(const LIBMTP_raw_device_t& rawDevice);
	std::string GetCurrentPanelItemDeviceName();
	std::wstring GeneratePanelTitle();
    
    // ID encoding/decoding utilities moved to MTPFileSystem
    static bool IsEncodedId(const std::string& str);
    
    // Helper methods for ID conversion
    std::string GetCurrentEncodedId() const;
    void SetCurrentFromEncodedId(const std::string& encodedId);
    
	bool ConnectToDevice(const std::string &deviceId);
	
	// Device enumeration handled by GetDeviceData

	// Basic navigation only - no file operations needed
	
	// far2l API access
	static PluginStartupInfo *GetInfo();
};
