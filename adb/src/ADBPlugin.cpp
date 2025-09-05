#include "ADBPlugin.h"
#include "ADBDevice.h"
#include "ADBShell.h"
#include "ADBDialogs.h"
#include <sstream>
#include <vector>
#include <cstring>
#include <cstdarg>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <utils.h>
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

PluginStartupInfo g_Info = {};

ADBPlugin::ADBPlugin(const wchar_t *path, bool path_is_standalone_config, int OpMode)
	: _allow_remember_location_dir(false)
	, _isFileMode(true)
	, _isConnected(false)
	, _deviceSerial("")
	, _devicePath("/")
{
	wcscpy(_panel_title, L"ADB Plugin");
	wcscpy(_cur_dir, L"/");
	wcscpy(_mk_dir, L"");
	wcscpy(_format, L"ADB");
	
	if (path && path_is_standalone_config) {
		_standalone_config = path;
	}
}

ADBPlugin::~ADBPlugin()
{
}

int ADBPlugin::GetFindData(PluginPanelItem **pPanelItem, int *pItemsNumber, int OpMode)
{
	if (_isConnected) {
		return GetDeviceFileData(pPanelItem, pItemsNumber, OpMode);
	}
	
	std::string deviceSerial = GetFirstAvailableDevice();
	
	if (deviceSerial.empty()) {
		*pItemsNumber = 1;
		*pPanelItem = new PluginPanelItem[1];
		
		PluginPanelItem &item = (*pPanelItem)[0];
		memset(&item, 0, sizeof(PluginPanelItem));
		
		item.FindData.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
		item.FindData.dwUnixMode = 0644;
		item.FindData.lpwszFileName = const_cast<wchar_t*>(L"No ADB devices found");
		
		return 1;
	}
	
	if (ConnectToDevice(deviceSerial)) {
		_isConnected = true;
		_isFileMode = true;
		_deviceSerial = deviceSerial;
		_devicePath = "/";
		
		swprintf(_panel_title, sizeof(_panel_title)/sizeof(wchar_t), L"%s", 
			StrMB2Wide(_devicePath).c_str());
		
		return GetDeviceFileData(pPanelItem, pItemsNumber, OpMode);
	} else {
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
		}
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
	
	if (_isFileMode) {
		Info->PanelModesArray = nullptr;
		Info->PanelModesNumber = 0;
		Info->StartPanelMode = 0;
		Info->StartSortMode = SM_NAME;
		Info->Format = _format;
	} else {
		SetupDeviceSelectionMode();
		Info->PanelModesArray = &_panel_mode;
		Info->PanelModesNumber = 1;
		Info->StartPanelMode = '0';
		Info->StartSortMode = 0;
		Info->Format = L"ADB";
	}
	
	Info->StartSortOrder = 0;
	Info->KeyBar = nullptr;
	Info->ShortcutData = nullptr;
}

int ADBPlugin::ProcessKey(int Key, unsigned int ControlState)
{
	if (Key == VK_RETURN && ControlState == 0 && !_isConnected) {
		return ByKey_TryEnterSelectedDevice() ? TRUE : FALSE;
	}
	return 0;
}

int ADBPlugin::GetDeviceFileData(PluginPanelItem **pPanelItem, int *pItemsNumber, int OpMode)
{
	if (!_adbDevice) {
		return 0;
	}
	
	try {
		std::vector<PluginPanelItem> files;
		std::string current_path = _adbDevice->DirectoryEnum(_devicePath, files);
		
		_devicePath = current_path;
		
		PluginPanelItem parentDir{};
		parentDir.FindData.lpwszFileName = wcsdup(L"..");
		parentDir.FindData.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
		parentDir.FindData.dwUnixMode = S_IFDIR | 0755;
		files.insert(files.begin(), parentDir);
		
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
		item.FindData.lpwszFileName = wcsdup(L"..");
		item.FindData.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
		item.FindData.dwUnixMode = S_IFDIR | 0755;
		
		return 1;
	}
}

int ADBPlugin::ExitDeviceFilePanel()
{
	if (_adbDevice) {
		_adbDevice->Disconnect();
		_adbDevice.reset();
	}
	
	_isConnected = false;
	_deviceSerial.clear();
	_devicePath = "/";
	wcscpy(_panel_title, L"ADB Plugin");
	
	return 1;
}

std::string ADBPlugin::GetCurrentPanelItemDeviceName()
{
	return GetFallbackDeviceName();
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

bool ADBPlugin::ByKey_TryEnterSelectedDevice()
{
	std::string deviceSerial = GetFirstAvailableDevice();
	if (deviceSerial.empty()) {
		return false;
	}
	
	if (!ConnectToDevice(deviceSerial)) {
		return false;
	}
	
	_isConnected = true;
	_devicePath = "/";
	UpdatePathInfo();
	return true;
}

bool ADBPlugin::ConnectToDevice(const std::string &deviceSerial)
{
	try {
		_adbDevice = std::make_shared<ADBDevice>(deviceSerial);
		
		if (!_adbDevice->IsConnected()) {
			return false;
		}
		
		return true;
		
	} catch (const std::exception &ex) {
		return false;
	}
}

void ADBPlugin::UpdatePathInfo()
{
	std::wstring panel_title = StrMB2Wide(_devicePath);
	
	if (panel_title.size() >= ARRAYSIZE(_panel_title)) {
		size_t rmlen = 4 + (panel_title.size() - ARRAYSIZE(_panel_title));
		panel_title.replace((panel_title.size() - rmlen) / 2, rmlen, L"...");
	}
	
	wcscpy(_panel_title, panel_title.c_str());
	wcscpy(_cur_dir, StrMB2Wide(_devicePath).c_str());
	
	std::wstring format_str = L"adb" + StrMB2Wide(_devicePath);
	wcscpy(_format, format_str.c_str());
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

int ADBPlugin::GetHighlightedDeviceIndex()
{
	static int currentSelection = 0;
	
	PluginStartupInfo *info = GetInfo();
	if (info) {
		try {
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
	
	return currentSelection;
}

PluginStartupInfo *ADBPlugin::GetInfo()
{
	return &g_Info;
}

int ADBPlugin::ProcessEventCommand(const wchar_t *cmd, HANDLE hPlugin)
{
	return FALSE;
}

void ADBPlugin::SetupDeviceSelectionMode() {
	memset(&_panel_mode, 0, sizeof(_panel_mode));
	
	_panel_mode.FullScreen = 0;
	_panel_mode.DetailedStatus = 1;
	
	_panel_mode.ColumnTypes = L"N,C0,C1,C2";
	_panel_mode.ColumnWidths = L"35,15,15,15";
	
	_panel_mode.StatusColumnTypes = L"N,C0,C1,C2";
	_panel_mode.StatusColumnWidths = L"35,15,15,15";
	
	static const wchar_t* columnTitles[] = {
		L"Device Name",
		L"Model",
		L"Serial Number",
		L"Port"
	};
	_panel_mode.ColumnTitles = columnTitles;
}

void ADBPlugin::SetupFileBrowsingMode() {
	memset(&_panel_mode, 0, sizeof(_panel_mode));
	
	_panel_mode.FullScreen = 0;
	_panel_mode.DetailedStatus = 1;
	
    _panel_mode.ColumnTypes = L"N,S,D,T,M,L";
    _panel_mode.ColumnWidths = L"30,10,12,8,12,8";
    
    _panel_mode.StatusColumnTypes = L"N,S,D,T,M,L";
    _panel_mode.StatusColumnWidths = L"30,10,12,8,12,8";
    
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
	
	if (target_dir == "..") {
		size_t lastSlash = _devicePath.find_last_of('/');
		if (lastSlash != std::string::npos && lastSlash > 0) {
			_devicePath = _devicePath.substr(0, lastSlash);
		} else {
			_devicePath = "/";
		}
		
		UpdatePathInfo();
		return TRUE;
	}
	
	if (!_adbDevice->SetDirectory(target_dir)) {
		return FALSE;
	}
	
	_devicePath = _adbDevice->GetCurrentWorkingDirectory();
	UpdatePathInfo();
	
	return TRUE;
}

int ADBPlugin::GetFiles(PluginPanelItem *PanelItem, int ItemsNumber, int Move, const wchar_t **DestPath, int OpMode) {
	DebugLog("GetFiles: ItemsNumber=%d, Move=%d, OpMode=0x%x\n", ItemsNumber, Move, OpMode);
	
	if (ItemsNumber <= 0 || !_isConnected || !_adbDevice || !PanelItem || !DestPath) {
		return FALSE;
	}
	
	std::string destPath;
	if (DestPath && DestPath[0]) {
		destPath = StrWide2MB(DestPath[0]);
	} else {
		destPath = _devicePath;
	}
	
	if (!(OpMode & OPM_SILENT)) {
		if (!ADBDialogs::AskCopyMove(Move != 0, false, destPath)) {
			return -1;
		}
	}
	
	if (OpMode & OPM_VIEW) {
		DebugLog("F3 View: ItemsNumber=%d\n", ItemsNumber);
		if (ItemsNumber > 0) {
			std::string fileName = StrWide2MB(PanelItem[0].FindData.lpwszFileName);
			DebugLog("F3 View: fileName='%s'\n", fileName.c_str());
			
			if (PanelItem[0].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				DebugLog("F3 View: Directory, returning FALSE\n");
				return FALSE;
			}
			
			std::string devicePath = _devicePath;
			if (devicePath.back() != '/') {
				devicePath += "/";
			}
			devicePath += fileName;
			DebugLog("F3 View: devicePath='%s'\n", devicePath.c_str());
			
			std::string localPath = destPath;
			if (localPath.back() == '/' || localPath.back() == '\\') {
				localPath += fileName;
			}
			DebugLog("F3 View: localPath='%s'\n", localPath.c_str());
			
			int result = _adbDevice->PullFile(devicePath, localPath);
			DebugLog("F3 View: PullFile result=%d\n", result);
			return (result == 0) ? TRUE : FALSE;
		}
		DebugLog("F3 View: No items, returning FALSE\n");
		return FALSE;
	}
	
	int successCount = 0;
	int lastErrorCode = 0;
	
	for (int i = 0; i < ItemsNumber; i++) {
		std::string fileName = StrWide2MB(PanelItem[i].FindData.lpwszFileName);
		
		std::string devicePath = _devicePath;
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
	DebugLog("PutFiles: ItemsNumber=%d, Move=%d, OpMode=0x%x\n", ItemsNumber, Move, OpMode);
	
	if (ItemsNumber <= 0 || !_isConnected || !_adbDevice || !PanelItem || !SrcPath) {
		return FALSE;
	}
	
	std::string srcPath = StrWide2MB(SrcPath);
	
	if (!(OpMode & OPM_SILENT)) {
		std::string destPath = _devicePath;
		if (!destPath.empty() && destPath.back() != '/') {
			destPath += "/";
		}
		
		if (!ADBDialogs::AskCopyMove(Move != 0, true, destPath)) {
			return -1;
		}
		
		if (destPath != _devicePath) {
			_devicePath = destPath;
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
		
		std::string devicePath = _devicePath;
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
		
		std::string devicePath = _devicePath;
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
	
	std::string devicePath = _devicePath;
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
