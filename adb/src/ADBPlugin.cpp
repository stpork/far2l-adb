#include "ADBPlugin.h"
#include "ADBDevice.h"
#include "ADBShell.h"
#include "ADBDialogs.h"
#include "ADBLog.h"
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdarg>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <utils.h>
#include <farplug-wide.h>


PluginStartupInfo g_Info = {};
FarStandardFunctions g_FSF = {};


ADBPlugin::ADBPlugin(const wchar_t *path, bool path_is_standalone_config, int OpMode)
	: _allow_remember_location_dir(false)
	, _isConnected(false)
	, _deviceSerial("")
	, _CurrentDir("/")
	, _lastEnteredDir("")
{
	wcscpy(_PanelTitle, L"ADB");
	wcscpy(_mk_dir, L"");
	
	if (path && path_is_standalone_config) {
		_standalone_config = path;
	}
	
	// Auto-connect logic: if only one device available, connect immediately
	int deviceCount = GetAvailableDeviceCount();
	
	if (deviceCount == 1) {
		std::string deviceSerial = GetFirstAvailableDevice();
		if (!deviceSerial.empty()) {
			if (ConnectToDevice(deviceSerial)) {
				_isConnected = true;
				_deviceSerial = deviceSerial;
				// Update panel title
				std::wstring panel_title = StrMB2Wide(GetCurrentDevicePath());
				if (panel_title.size() >= ARRAYSIZE(_PanelTitle)) {
					size_t rmlen = 4 + (panel_title.size() - ARRAYSIZE(_PanelTitle));
					panel_title.replace((panel_title.size() - rmlen) / 2, rmlen, L"...");
				}
				wcscpy(_PanelTitle, panel_title.c_str());
			}
		}
	} else if (deviceCount > 1) {
		// Multiple devices available, show device selection mode
		_isConnected = false;
		wcscpy(_PanelTitle, L"ADB - Select Device");
	}
}

ADBPlugin::~ADBPlugin()
{
}

int ADBPlugin::GetFindData(PluginPanelItem **pPanelItem, int *pItemsNumber, int OpMode)
{
	if (_isConnected && _adbDevice) 
		return GetFileData(pPanelItem, pItemsNumber);
	
	return GetDeviceData(pPanelItem, pItemsNumber);
}

void ADBPlugin::FreeFindData(PluginPanelItem *PanelItem, int ItemsNumber)
{
	if (PanelItem) {
		for (int i = 0; i < ItemsNumber; i++) {
			if (PanelItem[i].FindData.lpwszFileName) {
				free((void*)PanelItem[i].FindData.lpwszFileName);
			}
			if (PanelItem[i].Description) {
				free((void*)PanelItem[i].Description);
			}
			if (PanelItem[i].Owner) {
				free((void*)PanelItem[i].Owner);
			}
			if (PanelItem[i].Group) {
				free((void*)PanelItem[i].Group);
			}
			// Free custom column data for device selection
			if (PanelItem[i].CustomColumnData) {
				for (int j = 0; j < PanelItem[i].CustomColumnNumber; j++) {
					if (PanelItem[i].CustomColumnData[j]) {
						free((void*)PanelItem[i].CustomColumnData[j]);
					}
				}
				delete[] PanelItem[i].CustomColumnData;
			}
		}
		delete[] PanelItem;
	}
}

void ADBPlugin::GetOpenPluginInfo(OpenPluginInfo *Info)
{
	if (!Info) return;

	Info->StructSize       = sizeof(*Info);
	Info->HostFile         = nullptr;
	Info->InfoLines        = nullptr;
	Info->InfoLinesNumber  = 0;
	Info->DescrFiles       = nullptr;
	Info->DescrFilesNumber = 0;
	Info->KeyBar           = nullptr;
	Info->ShortcutData     = nullptr;

	// Panel mode storage
	static PanelMode connectedMode = {
		.ColumnTypes        = L"N,C0",
		.ColumnWidths       = L"0,0",
		.ColumnTitles       = nullptr, // set later
		.FullScreen         = 0,
		.DetailedStatus     = 1,
		.AlignExtensions    = 0,
		.CaseConversion     = 0,
		.StatusColumnTypes  = L"N,C0",
		.StatusColumnWidths = L"0,0",
		.Reserved           = {0, 0}
	};

	static const wchar_t* connectedTitles[] = { L"Name", L"Size" };

	static PanelMode deviceMode = {
		.ColumnTypes        = L"N,C0,C1,C2",
		.ColumnWidths       = L"0,30,0,8",
		.ColumnTitles       = nullptr, // set later
		.FullScreen         = 0,
		.DetailedStatus     = 1,
		.AlignExtensions    = 0,
		.CaseConversion     = 0,
		.StatusColumnTypes  = L"N,C0,C1,C2",
		.StatusColumnWidths = L"0,30,0,8",
		.Reserved           = {0, 0}
	};

	static const wchar_t* deviceTitles[] = { L"Serial Number", L"Device Name", L"Model", L"Port" };

	if (_isConnected) {
		connectedMode.ColumnTitles = connectedTitles;
		Info->PanelModesArray      = &connectedMode;
		Info->Flags                = OPIF_SHOWPRESERVECASE | OPIF_USEHIGHLIGHTING;
		Info->StartSortMode        = SM_NAME;

		static std::wstring curDir;
		static std::wstring format;
		curDir  = StrMB2Wide(_CurrentDir);
		format  = L"adb:" + curDir;

		Info->CurDir  = curDir.c_str();
		Info->Format  = format.c_str();
	} else {
		deviceMode.ColumnTitles = deviceTitles;
		Info->PanelModesArray   = &deviceMode;
		Info->Flags             = OPIF_SHOWPRESERVECASE | OPIF_USEHIGHLIGHTING | OPIF_SHOWNAMESONLY;
		Info->StartSortMode     = 0;

		Info->CurDir = L"";
		Info->Format = L"ADB";
	}

	Info->StartPanelMode = '4';
	Info->PanelModesNumber  = 0;
	Info->PanelTitle     = _PanelTitle;
}


int ADBPlugin::ProcessKey(int Key, unsigned int ControlState)
{
	// Handle ENTER in device selection mode (when not connected)
	if (!_isConnected && Key == VK_RETURN && ControlState == 0) {
		return ByKey_TryEnterSelectedDevice() ? TRUE : FALSE;
	}
	return FALSE;
}


int ADBPlugin::GetFileData(PluginPanelItem **pPanelItem, int *pItemsNumber)
{
	try {
		std::vector<PluginPanelItem> files;
		std::string current_path = _adbDevice->DirectoryEnum(GetCurrentDevicePath(), files);
		
		PluginPanelItem parentDir{};
		parentDir.FindData.lpwszFileName = wcsdup(L"..");
		parentDir.FindData.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
		parentDir.FindData.dwUnixMode = S_IFDIR | 0755;
		files.insert(files.begin(), parentDir);
		
		DBG("Created '..' entry with attributes: 0x%x, mode: 0%o\n", 
			parentDir.FindData.dwFileAttributes, parentDir.FindData.dwUnixMode);
		
		*pItemsNumber = files.size();
		*pPanelItem = new PluginPanelItem[files.size()];
		
		for (size_t i = 0; i < files.size(); i++) {
			(*pPanelItem)[i] = files[i];
		}
		
		return files.size();
		
	} catch (const std::exception &ex) {
		*pItemsNumber = 1;
		*pPanelItem = new PluginPanelItem[1];
		
		PluginPanelItem &item = (*pPanelItem)[0];
		memset(&item, 0, sizeof(PluginPanelItem));
		
		item.FindData.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
		item.FindData.dwUnixMode = 0644;
		item.FindData.lpwszFileName = const_cast<wchar_t*>(L"Error accessing device");
		
		return 1;
	}
}

int ADBPlugin::GetDeviceData(PluginPanelItem **pPanelItem, int *pItemsNumber)
{
	ADBShell tempShell;
	std::string output = ADBShell::adbExec("devices -l");
	DBG("ADB devices output: %s\n", output.c_str());
	
	if (output.empty()) {
		DBG("No ADB devices found\n");
		*pItemsNumber = 1;
		*pPanelItem = new PluginPanelItem[1];
		
		PluginPanelItem &item = (*pPanelItem)[0];
		memset(&item, 0, sizeof(PluginPanelItem));
		
		item.FindData.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
		//item.FindData.dwUnixMode = 0644;
		item.FindData.lpwszFileName = const_cast<wchar_t*>(L"No ADB devices found");
		
		return 1;
	}
	
	std::vector<PluginPanelItem> devices;
	std::istringstream stream(output);
	std::string line;
	
	// Skip header line
	if (std::getline(stream, line)) {
	}
	
	while (std::getline(stream, line)) {
		if (line.empty()) continue;
		
		// Parse device line: "serial    device product:model model:name device:name transport_id:1"
		std::istringstream lineStream(line);
		std::string serial, status, model, deviceName, usbPort;
		
		if (lineStream >> serial >> status) {
			if (serial != "List" && serial != "daemon" && serial != "starting" && serial != "adb" && status == "device") {
				// Parse additional fields
				std::string field;
				while (lineStream >> field) {
					if (field.find("model:") == 0) {
						model = field.substr(6); // Remove "model:" prefix
					} else if (field.find("usb:") == 0) {
						usbPort = field; // Keep "usb:" prefix
					}
				}
				
				// Get friendly device name using ADB settings commands
				deviceName = GetDeviceFriendlyName(serial);
				
				// Use model as fallback if no friendly name found
				if (deviceName.empty() && !model.empty()) {
					deviceName = model;
				}
				
				// Use serial as final fallback
				if (deviceName.empty()) {
					deviceName = serial;
				}
				
				PluginPanelItem device{};
				device.FindData.lpwszFileName = wcsdup(StrMB2Wide(serial).c_str());
				device.FindData.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY; // Make it look like a directory
				//device.FindData.dwUnixMode = S_IFDIR | 0755;
				
				// Store additional data in custom fields
				// C0=Serial Number, C1=Device Name, C2=Model, C3=Port
				wchar_t **customData = new wchar_t*[3];
				customData[0] = wcsdup(StrMB2Wide(deviceName).c_str()); // C1: Device Name
				customData[1] = wcsdup(StrMB2Wide(model).c_str());     // C2: Model
				customData[2] = wcsdup(StrMB2Wide(usbPort).c_str());   // C3: Port
				
				device.CustomColumnData = customData;
				device.CustomColumnNumber = 3;
				
				devices.push_back(device);
			}
		}
	}
	
	*pItemsNumber = devices.size();
	*pPanelItem = new PluginPanelItem[devices.size()];
	
	for (size_t i = 0; i < devices.size(); i++) {
		(*pPanelItem)[i] = devices[i];
	}
	
	return devices.size();
}

std::string ADBPlugin::GetDeviceFriendlyName(const std::string& serial)
{
	// Try to get device name from global settings first
	std::string cmd = "-s " + serial + " shell settings get global device_name";
	std::string output = ADBShell::adbExec(cmd);
	
	// Remove trailing newline if present
	if (!output.empty() && output.back() == '\n') {
		output.pop_back();
	}
	
	if (output.empty() || output == "null") {
		return "";
	}
	
	return output;
}

bool ADBPlugin::ByKey_TryEnterSelectedDevice()
{
	// Get the currently selected device from the panel
	std::string deviceSerial = GetCurrentPanelItemDeviceName();
	if (deviceSerial.empty()) {
		DBG("No device selected\n");
		return false;
	}
	
	DBG("Connecting to selected device: %s\n", deviceSerial.c_str());
	
	if (!ConnectToDevice(deviceSerial)) {
		DBG("Failed to connect to device: %s\n", deviceSerial.c_str());
		return false;
	}
	
	// Set up the connection state
	_isConnected = true;
	_deviceSerial = deviceSerial;
	// No need to update _devicePath - using GetCurrentDevicePath() directly
	
	// Update panel title with device serial and path
	std::wstring panel_title = StrMB2Wide(_deviceSerial) + L":" + StrMB2Wide(GetCurrentDevicePath());
	if (panel_title.size() >= ARRAYSIZE(_PanelTitle)) {
		size_t rmlen = 4 + (panel_title.size() - ARRAYSIZE(_PanelTitle));
		panel_title.replace((panel_title.size() - rmlen) / 2, rmlen, L"...");
	}
	wcscpy(_PanelTitle, panel_title.c_str());
	
	// Refresh the panel to show the new directory contents
	g_Info.Control(PANEL_ACTIVE, FCTL_UPDATEPANEL, 0, 0);
	
	// Reset cursor position to top of the panel
	PanelRedrawInfo ri = {};
	ri.CurrentItem = ri.TopPanelItem = 0;
	g_Info.Control(PANEL_ACTIVE, FCTL_REDRAWPANEL, 0, (LONG_PTR)&ri);
	
	DBG("Successfully connected to device: %s\n", deviceSerial.c_str());
	return true;
}

int ADBPlugin::GetHighlightedDeviceIndex()
{
	// For now, return 0 (first device)
	// In a full implementation, this would get the currently highlighted item
	return 0;
}

int ADBPlugin::ExitDeviceFilePanel()
{
	if (_adbDevice) {
		_adbDevice->Disconnect();
		_adbDevice.reset();
	}
	
	_isConnected = false;
	_deviceSerial.clear();
	// No need to update _devicePath - using GetCurrentDevicePath() directly
	wcscpy(_PanelTitle, L"ADB Plugin");
	
	return 1;
}

std::string ADBPlugin::GetCurrentPanelItemDeviceName()
{
	// Get the currently focused/selected item from the panel (following NetRocks pattern)
	intptr_t size = g_Info.Control(PANEL_ACTIVE, FCTL_GETSELECTEDPANELITEM, 0, 0);
	if (size < (intptr_t)sizeof(PluginPanelItem)) {
		DBG("No selected item or invalid size: %ld\n", (long)size);
		return "";
	}
	
	PluginPanelItem *item = (PluginPanelItem *)malloc(size + 0x100);
	if (!item) {
		DBG("Failed to allocate memory for panel item\n");
		return "";
	}
	
	// Clear the memory first
	memset(item, 0, size + 0x100);
	
	g_Info.Control(PANEL_ACTIVE, FCTL_GETSELECTEDPANELITEM, 0, (LONG_PTR)(void *)item);
	
	if (!item->FindData.lpwszFileName) {
		free(item);
		return "";
	}
	
	// Get serial number from filename (since we set it there)
	std::string deviceSerial = StrWide2MB(item->FindData.lpwszFileName);
	
	free(item);
	return deviceSerial;
}

std::string ADBPlugin::GetFallbackDeviceName()
{
	if (!_adbDevice) {
		return "Unknown Device";
	}
	
	std::string output = ADBShell::adbExec("devices -l");
	if (output.empty()) {
		return "No Device";
	}
	
	std::vector<std::string> deviceLines;
	std::istringstream outputStream(output);
	std::string line;
	
	if (std::getline(outputStream, line)) {
		while (std::getline(outputStream, line)) {
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
	
	int selectedIndex = GetHighlightedDeviceIndex();
	std::string deviceLine;
	
	if (selectedIndex >= 0 && selectedIndex < (int)deviceLines.size()) {
		deviceLine = deviceLines[selectedIndex];
	} else {
		deviceLine = deviceLines[0];
	}
	
	std::vector<std::string> tokens;
	std::string token;
	std::istringstream tokenStream(deviceLine);
	while (tokenStream >> token) {
		tokens.push_back(token);
	}
	
	if (tokens.size() >= 3) {
		for (size_t i = 2; i < tokens.size(); i++) {
			if (tokens[i].find("product:") == 0) {
				std::string product = tokens[i].substr(8);
				return product;
			}
			if (tokens[i].find("model:") == 0) {
				std::string model = tokens[i].substr(6);
				return model;
			}
		}
	}
	
	if (!tokens.empty()) {
		return tokens[0];
	}
	
	return "Unknown Device";
}

bool ADBPlugin::ConnectToDevice(const std::string &deviceSerial)
{
	DBG("ConnectToDevice: deviceSerial='%s'\n", deviceSerial.c_str());
	try {
		_adbDevice = std::make_shared<ADBDevice>(deviceSerial);
		DBG("ConnectToDevice: ADBDevice created\n");
		
		if (!_adbDevice->IsConnected()) {
			DBG("ConnectToDevice: ADBDevice not connected\n");
			return false;
		}
		
		DBG("ConnectToDevice: Successfully connected\n");
		return true;
		
	} catch (const std::exception &ex) {
		DBG("ConnectToDevice: Exception: %s\n", ex.what());
		return false;
	}
}

std::string ADBPlugin::GetFirstAvailableDevice()
{
	ADBShell tempShell;
	std::string output = ADBShell::adbExec("devices -l");
	if (output.empty()) {
		return "";
	}
	
	std::istringstream stream(output);
	std::string line;
	
	if (std::getline(stream, line)) {
	}
	
	while (std::getline(stream, line)) {
		if (line.empty()) continue;
		
		std::istringstream lineStream(line);
		std::string serial;
		if (lineStream >> serial) {
			if (serial != "List" && serial != "daemon" && serial != "starting" && serial != "adb") {
				return serial;
			}
		}
	}
	return "";
}

int ADBPlugin::GetAvailableDeviceCount()
{
	ADBShell tempShell;
	std::string output = ADBShell::adbExec("devices -l");
	if (output.empty()) {
		return 0;
	}
	
	std::istringstream stream(output);
	std::string line;
	int count = 0;
	
	// Skip header line
	if (std::getline(stream, line)) {
	}
	
	while (std::getline(stream, line)) {
		if (line.empty()) continue;
		
		std::istringstream lineStream(line);
		std::string serial;
		if (lineStream >> serial) {
			if (serial != "List" && serial != "daemon" && serial != "starting" && serial != "adb") {
				count++;
			}
		}
	}
	
	return count;
}

PluginStartupInfo *ADBPlugin::GetInfo()
{
	return &g_Info;
}

int ADBPlugin::ProcessEventCommand(const wchar_t *cmd, HANDLE hPlugin)
{
	// Force debug output to file immediately
	fprintf(stderr, "ProcessEventCommand called with cmd='%ls'\n", cmd ? cmd : L"NULL");
	fflush(stderr);
	
	DBG("Called with cmd='%ls'\n", cmd ? cmd : L"NULL");

	if (!cmd) {
		DBG("No command provided\n");
		return FALSE;
	}

	// Check if we're connected to a device - if not, we can't process any commands
	if (!_isConnected || !_adbDevice) {
		DBG("Not connected to device\n");
		return FALSE;
	}

	const wchar_t *commandToExecute = nullptr;

	// Check if command starts with "adb:" (explicit ADB command)
	if (wcsncasecmp(cmd, L"adb:", 4) == 0) {
		// Skip the "adb:" prefix
		commandToExecute = cmd + 4;
		DBG("Processing prefixed command: '%ls'\n", commandToExecute);
	} else {
		// Direct command when ADB panel is active
		commandToExecute = cmd;
		DBG("Processing direct command: '%ls'\n", commandToExecute);
	}

	// Skip leading spaces
	while (*commandToExecute == L' ') {
		commandToExecute++;
	}

	// Check if we have a command to execute
	if (*commandToExecute == L'\0') {
		DBG("Empty command after removing prefix and spaces\n");
		return FALSE;
	}

	DBG("About to execute command\n");
	
	// Execute the command in the existing ADB shell session
	std::string command = StrWide2MB(commandToExecute);
	DBG("Executing command '%s'\n", command.c_str());

	// Use the existing ADB shell session to execute the command
	std::string output = _adbDevice->RunShellCommand(command);
	DBG("Command output length=%zu\n", output.length());

	DBG("About to clear command line\n");
	/* Clear the command line
	DBG("Command line cleared\n");
	*/

	// Display the output in far2l console
	if (!output.empty()) {
		DBG("Output: '%s'\n", output.c_str());
		
		// Use read -r -d '' with heredoc to avoid escaping issues
		std::string readCmd = "read -r -d '' mytext <<'EOF'\n" + output + "\nEOF\necho \"$mytext\"";
		std::wstring wideReadCmd = StrMB2Wide(readCmd);
		DBG("Executing read with heredoc\n");
		
		if (g_Info.FSF && g_Info.FSF->Execute) {
			g_Info.FSF->Execute(wideReadCmd.c_str(), EF_NOCMDPRINT);
		}
	} else {
		DBG("No output from command\n");
	}
	
	// Clear command line after execution
	g_Info.Control(hPlugin, FCTL_SETCMDLINE, 0, (LONG_PTR)L"");
	
	return TRUE;
}

int ADBPlugin::SetDirectory(const wchar_t *Dir, int OpMode)
{
	if (!_isConnected || !_adbDevice || !Dir || wcslen(Dir) == 0) {
		return FALSE;
	}
	
	std::string target_dir = StrWide2MB(Dir);
	if (!_adbDevice->SetDirectory(target_dir)) {
		return FALSE;
	}
	
	// Remember the directory we're entering for cursor position tracking
	_lastEnteredDir = target_dir;
	
	// Update _CurrentDir to match the new directory
	_CurrentDir = _adbDevice->GetCurrentPath();
	
	// Update panel title with device serial and path
	std::wstring panel_title = StrMB2Wide(_deviceSerial) + L":" + StrMB2Wide(_CurrentDir);
	if (panel_title.size() >= ARRAYSIZE(_PanelTitle)) {
		size_t rmlen = 4 + (panel_title.size() - ARRAYSIZE(_PanelTitle));
		panel_title.replace((panel_title.size() - rmlen) / 2, rmlen, L"...");
	}
	wcscpy(_PanelTitle, panel_title.c_str());
	return TRUE;
}

int ADBPlugin::GetFiles(PluginPanelItem *PanelItem, int ItemsNumber, int Move, const wchar_t **DestPath, int OpMode) {
	DBG("ItemsNumber=%d, Move=%d, OpMode=0x%x\n", ItemsNumber, Move, OpMode);
	
	if (ItemsNumber <= 0 || !_isConnected || !_adbDevice || !PanelItem || !DestPath) {
		return FALSE;
	}
	
	std::string destPath;
	if (DestPath && DestPath[0]) {
		destPath = StrWide2MB(DestPath[0]);
	} else {
		destPath = GetCurrentDevicePath();
	}
	
	if (!(OpMode & OPM_SILENT)) {
		if (!ADBDialogs::AskCopyMove(Move != 0, false, destPath)) {
			return -1;
		}
	}
	
	if (OpMode & OPM_VIEW) {
		DBG("ItemsNumber=%d\n", ItemsNumber);
		if (ItemsNumber > 0) {
			std::string fileName = StrWide2MB(PanelItem[0].FindData.lpwszFileName);
			DBG("fileName='%s'\n", fileName.c_str());
			
			if (PanelItem[0].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				DBG("Directory, returning FALSE\n");
				return FALSE;
			}
			
			std::string devicePath = GetCurrentDevicePath();
			if (devicePath.back() != '/') {
				devicePath += "/";
			}
			devicePath += fileName;
			DBG("F3 View: devicePath='%s'\n", devicePath.c_str());
			
			std::string localPath = destPath;
			if (localPath.back() == '/' || localPath.back() == '\\') {
				localPath += fileName;
			}
			DBG("localPath='%s'\n", localPath.c_str());
			
			int result = _adbDevice->PullFile(devicePath, localPath);
			DBG("PullFile result=%d\n", result);
			return (result == 0) ? TRUE : FALSE;
		}
		DBG("No items, returning FALSE\n");
		return FALSE;
	}
	
	int successCount = 0;
	int lastErrorCode = 0;
	
	for (int i = 0; i < ItemsNumber; i++) {
		std::string fileName = StrWide2MB(PanelItem[i].FindData.lpwszFileName);
		
		std::string devicePath = GetCurrentDevicePath();
		if (devicePath.back() != '/') {
			devicePath += "/";
		}
		devicePath += fileName;
		
		std::string localPath = destPath;
		if (localPath.back() != '/' && localPath.back() != '\\') {
			localPath += "/";
		}
		localPath += fileName;
		
		bool success = false;
		
		int result = 0;
		if (PanelItem[i].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			result = _adbDevice->PullDirectory(devicePath, localPath);
			success = (result == 0);
		} else {
			result = _adbDevice->PullFile(devicePath, localPath);
			success = (result == 0);
		}
		
		if (success) {
			struct stat st;
			if (stat(localPath.c_str(), &st) != 0) {
				success = false;
			}
		}
		
		if (success) {
			successCount++;
		} else {
			lastErrorCode = result;
		}
	}
	
	if (successCount == 0) {
		WINPORT(SetLastError)(lastErrorCode);
	}
	
	return (successCount > 0) ? TRUE : FALSE;
}

int ADBPlugin::PutFiles(PluginPanelItem *PanelItem, int ItemsNumber, int Move, const wchar_t *SrcPath, int OpMode) {
	DBG("ItemsNumber=%d, Move=%d, OpMode=0x%x\n", ItemsNumber, Move, OpMode);
	
	if (ItemsNumber <= 0 || !_isConnected || !_adbDevice || !PanelItem || !SrcPath) {
		return FALSE;
	}
	
	std::string srcPath = StrWide2MB(SrcPath);
	
	if (!(OpMode & OPM_SILENT)) {
		std::string destPath = GetCurrentDevicePath();
		if (!destPath.empty() && destPath.back() != '/') {
			destPath += "/";
		}
		
		if (!ADBDialogs::AskCopyMove(Move != 0, true, destPath)) {
			return -1;
		}
		
		if (destPath != GetCurrentDevicePath()) {
			// Update ADBDevice's path when changing directory
			if (_adbDevice) {
				_adbDevice->SetDirectory(destPath);
			}
		}
	}
	
	int successCount = 0;
	int lastErrorCode = 0;
	
	for (int i = 0; i < ItemsNumber; i++) {
		std::string fileName = StrWide2MB(PanelItem[i].FindData.lpwszFileName);
		
		std::string localPath = srcPath;
		if (localPath.back() != '/' && localPath.back() != '\\') {
			localPath += "/";
		}
		localPath += fileName;
		
		std::string devicePath = GetCurrentDevicePath();
		if (devicePath.back() != '/') {
			devicePath += "/";
		}
		devicePath += fileName;
		
		bool success = false;
		
		int result = 0;
		if (PanelItem[i].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			result = _adbDevice->PushDirectory(localPath, devicePath);
			success = (result == 0);
		} else {
			result = _adbDevice->PushFile(localPath, devicePath);
			success = (result == 0);
		}
		
		if (success) {
			successCount++;
		} else {
			lastErrorCode = result;
		}
	}
	
	if (successCount == 0) {
		WINPORT(SetLastError)(lastErrorCode);
	}
	
	return (successCount > 0) ? TRUE : FALSE;
}

int ADBPlugin::ProcessHostFile(PluginPanelItem *PanelItem, int ItemsNumber, int OpMode) 
{
	return TRUE;
}

int ADBPlugin::DeleteFiles(PluginPanelItem *PanelItem, int ItemsNumber, int OpMode) {
	if (ItemsNumber <= 0 || !_isConnected || !_adbDevice || !PanelItem) {
		return FALSE;
	}
	
	if (!(OpMode & OPM_SILENT)) {
		std::wstring itemName;
		std::wstring itemType;
		
		if (ItemsNumber == 1) {
			itemName = PanelItem[0].FindData.lpwszFileName;
			if (PanelItem[0].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				itemType = L"the folder";
			} else {
				itemType = L"the file";
			}
		} else {
			itemName = std::to_wstring(ItemsNumber) + L" items";
			itemType = L"";
		}
		
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
		
		if (result != 0) {
			return -1;
		}
		
		bool needsRedDialog = false;
		bool hasMultipleItems = (ItemsNumber > 1);
		bool hasNonEmptyDirs = false;
		
		for (int i = 0; i < ItemsNumber; i++) {
			if (PanelItem[i].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				hasNonEmptyDirs = true;
				break;
			}
		}
		
		if (hasMultipleItems || hasNonEmptyDirs) {
			needsRedDialog = true;
		}
		
		if (needsRedDialog) {
			int fileCount = 0;
			int folderCount = 0;
			for (int i = 0; i < ItemsNumber; i++) {
				if (PanelItem[i].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
					folderCount++;
				} else {
					fileCount++;
				}
			}
			
			int redResult = 0;
			if (hasMultipleItems && !hasNonEmptyDirs) {
				redResult = ADBDialogs::Message(FMSG_WARNING | FMSG_MB_YESNO,
					L"Delete files",
					L"Do you wish to delete",
					std::to_wstring(ItemsNumber) + L" items");
			} else if (hasNonEmptyDirs && ItemsNumber == 1) {
				redResult = ADBDialogs::Message(FMSG_WARNING | FMSG_MB_YESNO,
					L"Delete folder",
					L"The following folder will be deleted:",
					L"/" + std::wstring(PanelItem[0].FindData.lpwszFileName));
			} else if (hasNonEmptyDirs && ItemsNumber > 1) {
				if (fileCount > 0 && folderCount > 0) {
					redResult = ADBDialogs::Message(FMSG_WARNING | FMSG_MB_YESNO,
						L"Delete items",
						L"The following items will be deleted:",
						std::to_wstring(folderCount) + L" folders",
						std::to_wstring(fileCount) + L" files");
				} else {
					redResult = ADBDialogs::Message(FMSG_WARNING | FMSG_MB_YESNO,
						L"Delete folders",
						L"The following folders will be deleted:",
						std::to_wstring(ItemsNumber) + L" folders");
				}
			}
			
			if (redResult != 0) {
				return -1;
			}
		}
	}
	
	int successCount = 0;
	int lastErrorCode = 0;
	
	for (int i = 0; i < ItemsNumber; i++) {
		std::string fileName = StrWide2MB(PanelItem[i].FindData.lpwszFileName);
		
		std::string devicePath = GetCurrentDevicePath();
		if (devicePath.back() != '/') {
			devicePath += "/";
		}
		devicePath += fileName;
		
		int result = 0;
		
		if (PanelItem[i].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			result = _adbDevice->DeleteDirectory(devicePath);
		} else {
			result = _adbDevice->DeleteFile(devicePath);
		}
		
		if (result == 0) {
			successCount++;
		} else {
			lastErrorCode = result;
		}
	}
	
	if (successCount == 0) {
		WINPORT(SetLastError)(lastErrorCode);
	}
	
	return (successCount > 0) ? TRUE : FALSE;
}

int ADBPlugin::MakeDirectory(const wchar_t **Name, int OpMode)
{
	if (!_isConnected || !_adbDevice) {
		return FALSE;
	}
	
	std::string dir_name;
	if (Name && *Name) {
		dir_name = StrWide2MB(*Name);
	}
	
	if (!(OpMode & OPM_SILENT)) {
		if (!ADBDialogs::AskCreateDirectory(dir_name)) {
			return -1;
		}
	}
	
	if (dir_name.empty()) {
		return FALSE;
	}
	
	std::string devicePath = GetCurrentDevicePath();
	if (!devicePath.empty() && devicePath.back() != '/') {
		devicePath += "/";
	}
	devicePath += dir_name;
	
	int result = _adbDevice->CreateDirectory(devicePath);
	if (result == 0) {
		if (Name && !(OpMode & OPM_SILENT)) {
			wcscpy(_mk_dir, StrMB2Wide(dir_name).c_str());
			*Name = _mk_dir;
		}
		
		return TRUE;
	} else {
		WINPORT(SetLastError)(result);
		return FALSE;
	}
}

std::string ADBPlugin::GetCurrentDevicePath() const
{
	if (_isConnected && _adbDevice) {
		return _adbDevice->GetCurrentPath();
	}
	return "/";
}
