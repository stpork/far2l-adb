// Local includes
#include "ADBPlugin.h"
#include "ADBDevice.h"
#include "ADBShell.h"
#include "ADBDialogs.h"

// Standard library includes
#include <sstream>
#include <vector>
#include <cstring>
#include <cstdarg>

// System includes
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

// far2l utilities
#include <utils.h>

// Debug logging
void DebugLog(const char* format, ...) {
    static FILE* logFile = nullptr;
    if (!logFile) {
        logFile = fopen("/tmp/adb_plugin_debug.log", "a");
    }
    if (logFile) {
        va_list args;
        va_start(args, format);
        vfprintf(logFile, format, args);
        fflush(logFile);
        va_end(args);
    }
}

// Global Info instance for far2l API access (like NetRocks)
PluginStartupInfo g_Info = {};

ADBPlugin::ADBPlugin(const wchar_t *path, bool path_is_standalone_config, int OpMode)
	: _allow_remember_location_dir(false)
	, _isFileMode(true)  // Start in file browsing mode
	, _isConnected(false)
	, _deviceSerial("")
	, _devicePath("/")
{
	// Initialize panel title and current directory
	wcscpy(_panel_title, L"ADB Plugin");
	wcscpy(_cur_dir, L"/");
	wcscpy(_mk_dir, L"");
	wcscpy(_format, L"ADB");
	
	// Store standalone config if provided
	if (path && path_is_standalone_config) {
		_standalone_config = path;
	}
}

ADBPlugin::~ADBPlugin()
{
	// Empty destructor
}

int ADBPlugin::GetFindData(PluginPanelItem **pPanelItem, int *pItemsNumber, int OpMode)
{
	// Check if we're connected to a device or in device selection mode
	if (_isConnected) {

		return GetDeviceFileData(pPanelItem, pItemsNumber, OpMode);
	}
	// AUTO-CONNECT: Instead of showing device selection, immediately connect to first available device
	std::string deviceSerial = GetFirstAvailableDevice();
	
	if (deviceSerial.empty()) {
		// Show error message
		*pItemsNumber = 1;
		*pPanelItem = new PluginPanelItem[1];
		
		PluginPanelItem &item = (*pPanelItem)[0];
		memset(&item, 0, sizeof(PluginPanelItem));
		
		item.FindData.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
		item.FindData.dwUnixMode = 0644;
		item.FindData.lpwszFileName = const_cast<wchar_t*>(L"No ADB devices found");
		
		return 1;
	}
	// Connect to the device immediately
	if (ConnectToDevice(deviceSerial)) {
		_isConnected = true;
		_isFileMode = true;  // We're now in file browsing mode
		_deviceSerial = deviceSerial;
		_devicePath = "/";
		
		// Update panel title to show current directory path only
		swprintf(_panel_title, sizeof(_panel_title)/sizeof(wchar_t), L"%s", 
			StrMB2Wide(_devicePath).c_str());
		
		// Now return the device file data instead of device selection
		return GetDeviceFileData(pPanelItem, pItemsNumber, OpMode);
	} else {
		// Show error message
		*pItemsNumber = 1;
		*pPanelItem = new PluginPanelItem[1];
		
		PluginPanelItem &item = (*pPanelItem)[0];
		memset(&item, 0, sizeof(PluginPanelItem));
		
		item.FindData.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
		item.FindData.dwUnixMode = 0644;
		item.FindData.lpwszFileName = const_cast<wchar_t*>(L"Failed to connect to ADB device");
		
		return 1;
	}
}

void ADBPlugin::FreeFindData(PluginPanelItem *PanelItem, int ItemsNumber)
{
	if (PanelItem) {
		delete[] PanelItem;
	}
}

void ADBPlugin::GetOpenPluginInfo(OpenPluginInfo *Info)
{
	if (!Info) {
		return;
	}
	
	Info->StructSize = sizeof(*Info);
	Info->Flags = OPIF_SHOWPRESERVECASE | OPIF_USEHIGHLIGHTING | OPIF_ADDDOTS;
	Info->HostFile = nullptr;
	Info->CurDir = _cur_dir;
	Info->PanelTitle = _panel_title;
	Info->InfoLines = nullptr;
	Info->InfoLinesNumber = 0;
	Info->DescrFiles = nullptr;
	Info->DescrFilesNumber = 0;
	
	// Dual panel mode approach (like NetRocks):
	// - Mode '0': Device selection panel (custom columns for device info)
	// - Mode '1': File panel (standard far2l columns for file browsing)
	
	if (_isFileMode) {
		// We're in file browsing mode - use standard far2l panel mode
		// Don't set custom panel modes to avoid hierarchical display issues
		Info->PanelModesArray = nullptr;
		Info->PanelModesNumber = 0;
		Info->StartPanelMode = 0;  // Use default panel mode
		Info->StartSortMode = SM_NAME;
		Info->Format = L"adb";
	} else {
		// We're in device selection mode - use custom panel mode
		SetupDeviceSelectionMode();
		Info->PanelModesArray = &_panel_mode;
		Info->PanelModesNumber = 1;
		Info->StartPanelMode = '0';  // Custom mode for device selection
		Info->StartSortMode = 0;
		Info->Format = L"ADB";
	}
	
	Info->StartSortOrder = 0;
	Info->KeyBar = nullptr;
	Info->ShortcutData = nullptr;
}

int ADBPlugin::ProcessKey(int Key, unsigned int ControlState)
{
	// Handle Enter key to enter selected device (NetRocks pattern)
	if (Key == VK_RETURN && ControlState == 0 && !_isConnected) {
		return ByKey_TryEnterSelectedDevice() ? TRUE : FALSE;
	}

	return 0; // Key not processed
}

int ADBPlugin::GetDeviceFileData(PluginPanelItem **pPanelItem, int *pItemsNumber, int OpMode)
{
	if (!_adbDevice) {
		return 0;
	}
	
	try {
		// Use ADBDevice to get directory contents
		// ADBDevice will handle the cd + pwd + ls -la sequence internally
		std::vector<PluginPanelItem> files;
		std::string current_path = _adbDevice->DirectoryEnum(_devicePath, files);
		
		// Update the current device path with the actual path returned by the device
		_devicePath = current_path;
		
		// Always add ".." entry to prevent plugin closure on empty directories (NetRocks pattern)
		PluginPanelItem parentDir{};
		parentDir.FindData.lpwszFileName = wcsdup(L"..");
		parentDir.FindData.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
		parentDir.FindData.dwUnixMode = S_IFDIR | 0755;
		files.insert(files.begin(), parentDir);
		
		// ADBDevice now returns PluginPanelItem directly, so we can use them directly
		*pItemsNumber = files.size();
		*pPanelItem = new PluginPanelItem[files.size()];
		
		// Copy the PluginPanelItem structs
		for (size_t i = 0; i < files.size(); i++) {
			(*pPanelItem)[i] = files[i];
		}
		
		return files.size();
		
	} catch (const std::exception &ex) {
		// Even on error, return at least the ".." entry to prevent plugin closure
		*pItemsNumber = 1;
		*pPanelItem = new PluginPanelItem[1];
		
		PluginPanelItem &item = (*pPanelItem)[0];
		memset(&item, 0, sizeof(PluginPanelItem));
		item.FindData.lpwszFileName = wcsdup(L"..");
		item.FindData.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
		item.FindData.dwUnixMode = S_IFDIR | 0755;
		
		return 1;
	}
}

// Exit device file panel mode
int ADBPlugin::ExitDeviceFilePanel()
{
	// Close ADBDevice connection
	if (_adbDevice) {
		_adbDevice->Disconnect();
		_adbDevice.reset();
	}
	
	// Reset state
	_isConnected = false;
	_deviceSerial.clear();
	_devicePath = "/";
	
	// Reset panel title
	wcscpy(_panel_title, L"ADB Plugin");
	
	return 1; // Success
}

// Get the device name from the currently highlighted panel item
std::string ADBPlugin::GetCurrentPanelItemDeviceName()
{
	return GetFallbackDeviceName();
}

// Fallback method to get device name when panel API fails
std::string ADBPlugin::GetFallbackDeviceName()
{
	// Use runAdbCommand to get device list
	if (!_adbDevice) {
		return "Unknown Device";
	}
	
	std::string output = ADBShell::adbExec("devices -l");
	if (output.empty()) {
		return "No Device";
	}
	
	// Parse the output to find device lines
	std::vector<std::string> deviceLines;
	std::istringstream outputStream(output);
	std::string line;
	
	// Skip header line
	if (std::getline(outputStream, line)) {
		// Read all device lines
		while (std::getline(outputStream, line)) {
			// Remove trailing newline
			if (!line.empty() && line[line.length()-1] == '\r') {
				line.erase(line.length()-1);
			}
			
			if (line.find("device") != std::string::npos) {
				deviceLines.push_back(line);
			}
		}
	}
	
	if (deviceLines.empty()) {
		return "No Device";
	}
	
	// Try to determine which device is highlighted by checking panel state
	// Since we can't use the panel API, we'll implement a simple selection mechanism
	int selectedIndex = GetHighlightedDeviceIndex();
	std::string deviceLine;
	
	if (selectedIndex >= 0 && selectedIndex < (int)deviceLines.size()) {
		deviceLine = deviceLines[selectedIndex];
	} else {
		// Fallback to first device if selection can't be determined
		deviceLine = deviceLines[0];
	}
	
	// Parse device line to get device name (try to find a meaningful name)
	std::vector<std::string> tokens;
	std::string token;
	std::istringstream tokenStream(deviceLine);
	while (tokenStream >> token) {
		tokens.push_back(token);
	}
	
	if (tokens.size() >= 3) {
		// Look for product or model in the tokens
		for (size_t i = 2; i < tokens.size(); i++) {
			if (tokens[i].find("product:") == 0) {
				std::string product = tokens[i].substr(8); // Remove "product:"
				return product;
			}
			if (tokens[i].find("model:") == 0) {
				std::string model = tokens[i].substr(6); // Remove "model:"
				return model;
			}
		}
	}
	
	// If no product/model found, return first token (serial) as fallback
	if (!tokens.empty()) {
		return tokens[0];
	}
	
	return "Unknown Device";
}

// Device connection methods (NetRocks pattern)

// Try to enter the selected device - now auto-connects to first available device
bool ADBPlugin::ByKey_TryEnterSelectedDevice()
{
	
	// Auto-connect to first available ADB device instead of device selection
	std::string deviceSerial = GetFirstAvailableDevice();
	if (deviceSerial.empty()) {
		return false;
	}
	
	// Establish ADB connection
	if (!ConnectToDevice(deviceSerial)) {
		return false;
	}
	
	// Switch to file navigation mode
	_isConnected = true;
	_devicePath = "/";
	
	// Force panel refresh (NetRocks pattern)
	UpdatePathInfo();
	return true;
}

// Connect to a specific device using ADBDevice
bool ADBPlugin::ConnectToDevice(const std::string &deviceSerial)
{
	
	try {
		// Create ADBDevice instance
		_adbDevice = std::make_shared<ADBDevice>(deviceSerial);
		
		if (!_adbDevice->IsConnected()) {
			return false;
		}
		
		return true;
		
	} catch (const std::exception &ex) {
		return false;
	}
}

// Update path information and force panel refresh (NetRocks pattern)
void ADBPlugin::UpdatePathInfo()
{
	// Update panel title to show only the absolute path from pwd
	std::wstring panel_title = StrMB2Wide(_devicePath);
	
	// Truncate if too long (NetRocks pattern)
	if (panel_title.size() >= ARRAYSIZE(_panel_title)) {
		size_t rmlen = 4 + (panel_title.size() - ARRAYSIZE(_panel_title));
		panel_title.replace((panel_title.size() - rmlen) / 2, rmlen, L"...");
	}
	
	wcscpy(_panel_title, panel_title.c_str());
	
	// Update current directory
	wcscpy(_cur_dir, StrMB2Wide(_devicePath).c_str());
}

// Get the first available ADB device (auto-select)
std::string ADBPlugin::GetFirstAvailableDevice()
{
	
	// Use AdbShell to execute 'adb devices -l' command
	ADBShell tempShell;
	std::string output = ADBShell::adbExec("devices -l");
	if (output.empty()) {
		return "";
	}
	
	
	// Parse the output to find the first device
	std::istringstream stream(output);
	std::string line;
	
	// Skip the first line ("List of devices attached")
	if (std::getline(stream, line)) {
		// Skip header line
	}
	
	// Look for the first device line
	while (std::getline(stream, line)) {
		if (line.empty()) continue;
		
		// Parse device line format: "serial    device"
		std::istringstream lineStream(line);
		std::string serial;
		if (lineStream >> serial) {
			// Check if it's a valid device (not "List of devices attached")
			if (serial != "List" && serial != "daemon" && serial != "starting" && serial != "adb") {
				return serial;
			}
		}
	}
	return "";
}

// Get the index of the currently highlighted device
int ADBPlugin::GetHighlightedDeviceIndex()
{
	static int currentSelection = 0; // Start with first device
	
	// Try to get selection from far2l if possible
	PluginStartupInfo *info = GetInfo();
	if (info) {
		try {
			// Try to get panel info to determine current selection
			PanelInfo panelInfo = {};
			if (info->Control(PANEL_ACTIVE, FCTL_GETPANELINFO, 0, (LONG_PTR)&panelInfo)) {
				if (panelInfo.CurrentItem >= 0 && panelInfo.CurrentItem < panelInfo.ItemsNumber) {
					currentSelection = panelInfo.CurrentItem;
					return currentSelection;
				}
			}
		} catch (...) {
		}
	}
	
	// Return stored selection
	return currentSelection;
}

// Static method to get the global Info instance
PluginStartupInfo *ADBPlugin::GetInfo()
	{
		// Return pointer to global instance
		return &g_Info;
	}

int ADBPlugin::ProcessEventCommand(const wchar_t *cmd, HANDLE hPlugin)
{
	// Command execution disabled for now
	return FALSE;
}

// Setup panel mode for device selection (like NetRocks host selection)
void ADBPlugin::SetupDeviceSelectionMode() {
	// Initialize panel mode structure
	memset(&_panel_mode, 0, sizeof(_panel_mode));
	
	// Set panel mode flags - use default panel view (half screen), not full screen
	_panel_mode.FullScreen = 0;        // Panel mode (half screen)
	_panel_mode.DetailedStatus = 1;    // Show detailed status
	
	// Define column types - N = Name column, C0-C2 = Custom columns for device details
	// We want: Device Name, Model, Serial Number, Port
	_panel_mode.ColumnTypes = L"N,C0,C1,C2";
	_panel_mode.ColumnWidths = L"35,15,15,15";
	
	// Set status column types and widths (same as main columns)
	_panel_mode.StatusColumnTypes = L"N,C0,C1,C2";
	_panel_mode.StatusColumnWidths = L"35,15,15,15";
	
	// Set column titles
	static const wchar_t* columnTitles[] = {
		L"Device Name",
		L"Model",
		L"Serial Number",
		L"Port"
	};
	_panel_mode.ColumnTitles = columnTitles;
}

// Setup custom columns for ADB file browsing panel
void ADBPlugin::SetupFileBrowsingMode() {
	// Initialize panel mode structure
	memset(&_panel_mode, 0, sizeof(_panel_mode));
	
	// Set panel mode flags - use default panel view (half screen), not full screen
	_panel_mode.FullScreen = 0;        // Panel mode (half screen)
	_panel_mode.DetailedStatus = 1;    // Show detailed status
	
	    // Define column types - N = Name column, S = Size, D = Date, T = Time, M = Mode, L = Links
    // We want: Name, Size, Date, Time, Mode (permissions), Links
    _panel_mode.ColumnTypes = L"N,S,D,T,M,L";
    _panel_mode.ColumnWidths = L"30,10,12,8,12,8";
    
    // Set status column types and widths (same as main columns)
    _panel_mode.StatusColumnTypes = L"N,S,D,T,M,L";
    _panel_mode.StatusColumnWidths = L"30,10,12,8,12,8";
    
    // Set column titles
    static const wchar_t* columnTitles[] = {
        L"Name",
        L"Size",
        L"Date",
        L"Time",
        L"Mode",
        L"Links"
    };
    _panel_mode.ColumnTitles = columnTitles;
}

int ADBPlugin::SetDirectory(const wchar_t *Dir, int OpMode)
{
	if (!_isConnected || !_adbDevice || !Dir || wcslen(Dir) == 0) {
		return FALSE;
	}
	
	std::string target_dir = StrWide2MB(Dir);
	
	// Handle ".." directory navigation
	if (target_dir == "..") {
		// Navigate to parent directory
		size_t lastSlash = _devicePath.find_last_of('/');
		if (lastSlash != std::string::npos && lastSlash > 0) {
			_devicePath = _devicePath.substr(0, lastSlash);
		} else {
			_devicePath = "/";
		}
		
		// Update panel title and current directory display
		UpdatePathInfo();
		return TRUE;
	}
	
	// Call protocol to change directory
	if (!_adbDevice->SetDirectory(target_dir)) {
		return FALSE;
	}
	
	// Get the actual current path from the protocol
	_devicePath = _adbDevice->GetCurrentWorkingDirectory();
	
	// Update panel title and current directory display (NetRocks pattern)
	UpdatePathInfo();
	return TRUE;
}

int ADBPlugin::GetFiles(PluginPanelItem *PanelItem, int ItemsNumber, int Move, const wchar_t **DestPath, int OpMode) {
	
	// Debug: Print all arguments
	DebugLog("GetFiles called with:\n");
	DebugLog("  ItemsNumber: %d\n", ItemsNumber);
	DebugLog("  Move: %d\n", Move);
	DebugLog("  OpMode: 0x%llx\n", (unsigned long long)OpMode);
	
	const char* flags = "";
	if (OpMode & OPM_SILENT) flags = "SILENT ";
	if (OpMode & OPM_FIND) flags = "FIND ";
	if (OpMode & OPM_VIEW) flags = "VIEW ";
	if (OpMode & OPM_EDIT) flags = "EDIT ";
	if (OpMode & OPM_QUICKVIEW) flags = "QUICKVIEW ";
	if (OpMode & OPM_DESCR) flags = "DESCR ";
	DebugLog("  OpMode flags: %s\n", flags);
	
	if (PanelItem && ItemsNumber > 0) {
		DebugLog("  First file: %ls\n", PanelItem[0].FindData.lpwszFileName);
		DebugLog("  File attributes: 0x%x\n", PanelItem[0].FindData.dwFileAttributes);
	}
	
	if (DestPath && DestPath[0]) {
		DebugLog("  DestPath: %ls\n", DestPath[0]);
	}
	
	if (ItemsNumber <= 0 || !_isConnected || !_adbDevice || !PanelItem || !DestPath || !DestPath[0]) {
		return FALSE;
	}
	
	std::string destPath = StrWide2MB(DestPath[0]);
	DebugLog("  destPath (converted): '%s'\n", destPath.c_str());
	
	// Show copy/move confirmation dialog unless in silent mode
	if (!(OpMode & OPM_SILENT)) {
		if (!ADBDialogs::AskCopyMove(Move != 0, false, destPath)) { // false = download (GetFiles)
			DebugLog("GetFiles: User cancelled copy/move dialog\n");
			return -1;  // Return -1 for user cancellation (like NetRocks)
		}
		DebugLog("GetFiles: User confirmed, destination: '%s'\n", destPath.c_str());
	}
	
	// Handle View operations (F3 View and F3 QuickView) - only process the first selected file
	if (OpMode & OPM_VIEW) {
		if (ItemsNumber > 0) {
			std::string fileName = StrWide2MB(PanelItem[0].FindData.lpwszFileName);
			
			// Skip directories for View operations
			if (PanelItem[0].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				return FALSE;
			}
			
			// Construct full device path
			std::string devicePath = _devicePath;
			if (devicePath.back() != '/') {
				devicePath += "/";
			}
			devicePath += fileName;
			
			// For View operations, download to the exact destination path (temporary file)
			std::string localPath = destPath;
			if (localPath.back() == '/' || localPath.back() == '\\') {
				localPath += fileName;
			}
			
			// Debug logging
			DebugLog("ADB View: devicePath='%s', localPath='%s'\n", devicePath.c_str(), localPath.c_str());
			
			// Use ADB pull to transfer file for View operations
			int result = _adbDevice->PullFile(devicePath, localPath);
			DebugLog("ADB Pull result: %s\n", result ? "SUCCESS" : "FAILED");
			return result ? TRUE : FALSE;
		}
		return FALSE;
	}
	
			// Handle regular file operations (F5 copy, etc.)
	int successCount = 0;
	int lastErrorCode = 0;
	
	for (int i = 0; i < ItemsNumber; i++) {
		std::string fileName = StrWide2MB(PanelItem[i].FindData.lpwszFileName);
		
		// Construct full device path
		std::string devicePath = _devicePath;
		if (devicePath.back() != '/') {
			devicePath += "/";
		}
		devicePath += fileName;
		
		// Construct local destination path
		std::string localPath = destPath;
		if (localPath.back() != '/' && localPath.back() != '\\') {
			localPath += "/";
		}
		localPath += fileName;
		
		bool success = false;
		
		// Handle directories and files differently
		int result = 0;
		if (PanelItem[i].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			// Use ADB pull directory transfer
			DebugLog("GetFiles: Starting directory transfer '%s' -> '%s'\n", 
				devicePath.c_str(), localPath.c_str());
			result = _adbDevice->PullDirectory(devicePath, localPath);
			success = (result == 0);
			DebugLog("GetFiles: Directory transfer '%s' -> '%s': %s\n", 
				devicePath.c_str(), localPath.c_str(), success ? "SUCCESS" : "FAILED");
		} else {
			// Use ADB pull file transfer
			DebugLog("GetFiles: Starting file transfer '%s' -> '%s'\n", 
				devicePath.c_str(), localPath.c_str());
			result = _adbDevice->PullFile(devicePath, localPath);
			success = (result == 0);
			DebugLog("GetFiles: File transfer '%s' -> '%s': %s\n", 
				devicePath.c_str(), localPath.c_str(), success ? "SUCCESS" : "FAILED");
		}
		
		// Additional verification after transfer
		if (success) {
			struct stat st;
			if (stat(localPath.c_str(), &st) == 0) {
				DebugLog("GetFiles: File exists after transfer: %s (size: %ld, mode: %o)\n", 
					localPath.c_str(), st.st_size, st.st_mode & 0777);
			} else {
				DebugLog("GetFiles: ERROR - File does not exist after transfer: %s\n", localPath.c_str());
				success = false; // Mark as failed if file doesn't exist
			}
		}
		
		if (success) {
			successCount++;
		} else {
			// Keep track of the last error code for proper error reporting
			lastErrorCode = result;
		}
	}
	
	// Set appropriate error code if no files were transferred
	if (successCount == 0) {
		WINPORT(SetLastError)(lastErrorCode); // Use the actual error code from ADB
	}
	
	return (successCount > 0) ? TRUE : FALSE;
}

int ADBPlugin::PutFiles(PluginPanelItem *PanelItem, int ItemsNumber, int Move, const wchar_t *SrcPath, int OpMode) {
	
	if (ItemsNumber <= 0 || !_isConnected || !_adbDevice || !PanelItem || !SrcPath) {
		return FALSE;
	}
	
	std::string srcPath = StrWide2MB(SrcPath);
	
	// Show copy/move confirmation dialog unless in silent mode
	if (!(OpMode & OPM_SILENT)) {
		std::string destPath = _devicePath;
		if (!destPath.empty() && destPath.back() != '/') {
			destPath += "/";
		}
		
		if (!ADBDialogs::AskCopyMove(Move != 0, true, destPath)) { // true = upload (PutFiles)
			DebugLog("PutFiles: User cancelled copy/move dialog\n");
			return -1;  // Return -1 for user cancellation (like NetRocks)
		}
		DebugLog("PutFiles: User confirmed, destination: '%s'\n", destPath.c_str());
		
		// Update the device path if user specified a different destination
		if (destPath != _devicePath) {
			_devicePath = destPath;
		}
	}
	
	int successCount = 0;
	int lastErrorCode = 0;
	
	for (int i = 0; i < ItemsNumber; i++) {
		std::string fileName = StrWide2MB(PanelItem[i].FindData.lpwszFileName);
		
		// Construct full local source path
		std::string localPath = srcPath;
		if (localPath.back() != '/' && localPath.back() != '\\') {
			localPath += "/";
		}
		localPath += fileName;
		
		// Construct device destination path
		std::string devicePath = _devicePath;
		if (devicePath.back() != '/') {
			devicePath += "/";
		}
		devicePath += fileName;
		
		bool success = false;
		
		// Handle directories and files differently
		int result = 0;
		if (PanelItem[i].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			// Use ADB push directory transfer
			result = _adbDevice->PushDirectory(localPath, devicePath);
			success = (result == 0);
			DebugLog("PutFiles: Directory transfer '%s' -> '%s': %s\n", 
				localPath.c_str(), devicePath.c_str(), success ? "SUCCESS" : "FAILED");
		} else {
			// Use ADB push file transfer
			result = _adbDevice->PushFile(localPath, devicePath);
			success = (result == 0);
			DebugLog("PutFiles: File transfer '%s' -> '%s': %s\n", 
				localPath.c_str(), devicePath.c_str(), success ? "SUCCESS" : "FAILED");
		}
		
		if (success) {
			successCount++;
		} else {
			// Keep track of the last error code for proper error reporting
			lastErrorCode = result;
		}
	}
	
	// Set appropriate error code if no files were transferred
	if (successCount == 0) {
		WINPORT(SetLastError)(lastErrorCode); // Use the actual error code from ADB
	}
	
	return (successCount > 0) ? TRUE : FALSE;
}

int ADBPlugin::ProcessHostFile(PluginPanelItem *PanelItem, int ItemsNumber, int OpMode) 
{
	return TRUE;
}

int ADBPlugin::DeleteFiles(PluginPanelItem *PanelItem, int ItemsNumber, int OpMode) {
	DebugLog("DeleteFiles called with ItemsNumber=%d, OpMode=0x%x\n", ItemsNumber, OpMode);
	
	if (ItemsNumber <= 0 || !_isConnected || !_adbDevice || !PanelItem) {
		return FALSE;
	}
	
	// Show confirmation dialog unless in silent mode
	if (!(OpMode & OPM_SILENT)) {
		// Stage 1: Normal confirmation dialog
		std::wstring itemName;
		std::wstring itemType;
		
		if (ItemsNumber == 1) {
			itemName = PanelItem[0].FindData.lpwszFileName;
			// Determine if it's a file or directory based on attributes
			if (PanelItem[0].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				itemType = L"the folder";
			} else {
				itemType = L"the file";
			}
		} else {
			itemName = std::to_wstring(ItemsNumber) + L" items";
			itemType = L""; // For multiple items, assume files
		}
		
		// Use the new variadic template Message function
		int result;
		if (!itemType.empty()) {
			result = ADBDialogs::Message(FMSG_MB_YESNO,
				L"Delete",
				L"Do you wish to delete",
				itemType,
				itemName);
		} else {
			result = ADBDialogs::Message(FMSG_MB_YESNO,
				L"Delete",
				L"Do you wish to delete",
				itemName);
		}
		
		if (result != 0) { // 0 = Yes, 1 = No
			DebugLog("DeleteFiles: User cancelled at stage 1\n");
			return -1;  // Return -1 for user cancellation (like NetRocks)
		}
		
		// Stage 2: Check if we need RED confirmation dialog
		bool needsRedDialog = false;
		bool hasMultipleItems = (ItemsNumber > 1);
		bool hasNonEmptyDirs = false;
		
		// Check for non-empty directories
		for (int i = 0; i < ItemsNumber; i++) {
			if (PanelItem[i].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				// For now, assume all directories are non-empty (we can't easily check on device)
				hasNonEmptyDirs = true;
				break;
			}
		}
		
		// Determine if we need RED dialog
		if (hasMultipleItems || hasNonEmptyDirs) {
			needsRedDialog = true;
		}
		
		if (needsRedDialog) {
			// Count files and folders separately
			int fileCount = 0;
			int folderCount = 0;
			for (int i = 0; i < ItemsNumber; i++) {
				if (PanelItem[i].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
					folderCount++;
				} else {
					fileCount++;
				}
			}
			
			// Use the new variadic template Message function for RED dialog
			int redResult = 0; // Initialize to avoid warning
			if (hasMultipleItems && !hasNonEmptyDirs) {
				// Multiple files only
				redResult = ADBDialogs::Message(FMSG_WARNING | FMSG_MB_YESNO,
					L"Delete files",
					L"Do you wish to delete",
					std::to_wstring(ItemsNumber) + L" items");
			} else if (hasNonEmptyDirs && ItemsNumber == 1) {
				// Single non-empty directory
				redResult = ADBDialogs::Message(FMSG_WARNING | FMSG_MB_YESNO,
					L"Delete folder",
					L"The following folder will be deleted:",
					L"/" + std::wstring(PanelItem[0].FindData.lpwszFileName));
			} else if (hasNonEmptyDirs && ItemsNumber > 1) {
				// Multiple items with folders (could be mix of files and folders)
				if (fileCount > 0 && folderCount > 0) {
					// Mixed files and folders
					redResult = ADBDialogs::Message(FMSG_WARNING | FMSG_MB_YESNO,
						L"Delete items",
						L"The following items will be deleted:",
						std::to_wstring(folderCount) + L" folders",
						std::to_wstring(fileCount) + L" files");
				} else {
					// Multiple folders only
					redResult = ADBDialogs::Message(FMSG_WARNING | FMSG_MB_YESNO,
						L"Delete folders",
						L"The following folders will be deleted:",
						std::to_wstring(ItemsNumber) + L" folders");
				}
			}
			
			if (redResult != 0) { // 0 = All/Delete, 1 = Cancel
				DebugLog("DeleteFiles: User cancelled at stage 2 (RED dialog)\n");
				return -1;  // Return -1 for user cancellation (like NetRocks)
			}
		}
	}
	
	// Process each selected item
	int successCount = 0;
	int lastErrorCode = 0;
	
	for (int i = 0; i < ItemsNumber; i++) {
		std::string fileName = StrWide2MB(PanelItem[i].FindData.lpwszFileName);
		
		// Construct full device path
		std::string devicePath = _devicePath;
		if (devicePath.back() != '/') {
			devicePath += "/";
		}
		devicePath += fileName;
		
		DebugLog("DeleteFiles: Attempting to delete '%s'\n", devicePath.c_str());
		
		int result = 0;
		
		// Check if it's a directory or file
		if (PanelItem[i].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			result = _adbDevice->DeleteDirectory(devicePath);
		} else {
			result = _adbDevice->DeleteFile(devicePath);
		}
		
		if (result == 0) {
			successCount++;
			DebugLog("DeleteFiles: Successfully deleted '%s'\n", devicePath.c_str());
		} else {
			DebugLog("DeleteFiles: Failed to delete '%s'\n", devicePath.c_str());
			lastErrorCode = result; // Keep the last error code for display
		}
	}
	
	DebugLog("DeleteFiles: Successfully deleted %d/%d items\n", successCount, ItemsNumber);
	
	// Set appropriate error code if no items were deleted
	if (successCount == 0) {
		WINPORT(SetLastError)(lastErrorCode);
	}
	
	// Return FALSE if no items were deleted, TRUE if some were deleted
	return (successCount > 0) ? TRUE : FALSE;
}

int ADBPlugin::MakeDirectory(const wchar_t **Name, int OpMode)
{
	DebugLog("MakeDirectory: OpMode=0x%x\n", OpMode);
	
	if (!_isConnected || !_adbDevice) {
		DebugLog("MakeDirectory: Not connected to device\n");
		return FALSE;
	}
	
	std::string dir_name;
	if (Name && *Name) {
		dir_name = StrWide2MB(*Name);
	}
	
	// Show input dialog unless in silent mode
	if (!(OpMode & OPM_SILENT)) {
		if (!ADBDialogs::AskCreateDirectory(dir_name)) {
			DebugLog("MakeDirectory: User cancelled (ESC pressed)\n");
			return -1;  // Return -1 for user cancellation (like NetRocks)
		}
	}
	
	if (dir_name.empty()) {
		DebugLog("MakeDirectory: Empty directory name\n");
		return FALSE;
	}
	
	// Create the directory on the device
	std::string devicePath = _devicePath;
	if (!devicePath.empty() && devicePath.back() != '/') {
		devicePath += "/";
	}
	devicePath += dir_name;
	
	DebugLog("MakeDirectory: Creating directory '%s'\n", devicePath.c_str());
	
	int result = _adbDevice->CreateDirectory(devicePath);
	if (result == 0) {
		DebugLog("MakeDirectory: Successfully created directory '%s'\n", devicePath.c_str());
		
		// Update the name parameter if provided
		if (Name && !(OpMode & OPM_SILENT)) {
			wcscpy(_mk_dir, StrMB2Wide(dir_name).c_str());
			*Name = _mk_dir;
		}
		
		return TRUE;
	} else {
		DebugLog("MakeDirectory: Failed to create directory '%s'\n", devicePath.c_str());
		
		// Set the error code returned by ADBDevice
		WINPORT(SetLastError)(result);
		
		return FALSE; // Return FALSE to indicate operation failed (consistent with NetRocks/farftp)
	}
}
