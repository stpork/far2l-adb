#include "MTPPlugin.h"
#include "MTPLog.h"
#include "MTPDialogs.h"
#include "farplug-wide.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <WideMB.h>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <IntStrConv.h>

static MTPPlugin* g_plugin = nullptr;
PluginStartupInfo g_Info;
FarStandardFunctions g_FSF;

MTPPlugin::MTPPlugin(const wchar_t *path, bool path_is_standalone_config, int OpMode)
    : _standalone_config(path ? path : L"")
    , _allow_remember_location_dir(true)
    , _isConnected(false)
    , _currentStorageID(0)
    , _currentDirID(0)
{
    wcscpy(_PanelTitle, L"MTP Device");
    _mtpDevice = std::make_shared<MTPDevice>("");
    _mtpFileSystem = std::make_shared<MTPFileSystem>(_mtpDevice);
    g_plugin = this;
    
    // Auto-connect to first available MTP device (similar to ADB plugin)
    DBG("MTPPlugin: Auto-connecting to first available MTP device");
    LIBMTP_Init();
    
    LIBMTP_raw_device_t* rawdevices;
    int numrawdevices;
    LIBMTP_error_number_t err = LIBMTP_Detect_Raw_Devices(&rawdevices, &numrawdevices);
    
    if (err != LIBMTP_ERROR_NONE) {
        DBG("MTPPlugin: Failed to detect MTP devices: %d", err);
        LIBMTP_FreeMemory(rawdevices);
        return;
    }
    
    if (numrawdevices == 0) {
        DBG("MTPPlugin: No MTP devices found");
        LIBMTP_FreeMemory(rawdevices);
        return;
    }
    
    // Connect to first available device
    std::string deviceId = std::to_string(rawdevices[0].bus_location) + "_" + std::to_string(rawdevices[0].devnum);
    DBG("MTPPlugin: Attempting to connect to first device: %s", deviceId.c_str());
    
    if (ConnectToDevice(deviceId)) {
        DBG("MTPPlugin: Successfully auto-connected to MTP device");
    } else {
        DBG("MTPPlugin: Failed to auto-connect to MTP device");
    }
    
    LIBMTP_FreeMemory(rawdevices);
}

MTPPlugin::~MTPPlugin()
{
    if (_mtpDevice) {
        _mtpDevice->Disconnect();
    }
    g_plugin = nullptr;
}

int MTPPlugin::GetFindData(PluginPanelItem **pPanelItem, int *pItemsNumber, int OpMode)
{
    if (!_isConnected) {
        return GetDeviceData(pPanelItem, pItemsNumber);
    } else {
        return GetFileData(pPanelItem, pItemsNumber);
    }
}

void MTPPlugin::FreeFindData(PluginPanelItem *PanelItem, int ItemsNumber)
{
    DBG("FreeFindData called with %d items", ItemsNumber);
    if (PanelItem) {
        for (int i = 0; i < ItemsNumber; i++) {
            DBG("FreeFindData item %d: UserData=%p, lpwszFileName=%p", i, (void*)PanelItem[i].UserData, PanelItem[i].FindData.lpwszFileName);
            if (PanelItem[i].FindData.lpwszFileName) {
                free((void*)PanelItem[i].FindData.lpwszFileName);
            }
            if (PanelItem[i].Description) {
                free((void*)PanelItem[i].Description);
            }
            // Don't free UserData for device items - they contain device ID strings
            // that need to persist across panel refreshes
        }
        free(PanelItem);
    }
}

void MTPPlugin::GetOpenPluginInfo(OpenPluginInfo *Info)
{
    Info->StructSize = sizeof(OpenPluginInfo);
    Info->Flags = OPIF_SHOWPRESERVECASE | OPIF_USEHIGHLIGHTING | OPIF_ADDDOTS;
    Info->HostFile = nullptr;
    
    // Set CurDir for cursor position restoration
    if (_currentDirID != 0 && _mtpDevice) {
        LIBMTP_file_t* objectFile = LIBMTP_Get_Filemetadata(_mtpDevice->GetDevice(), _currentDirID);
        if (objectFile && objectFile->filename) {
            std::string dirName = std::string(objectFile->filename);
            std::wstring wideDir = StrMB2Wide(dirName);
            Info->CurDir = (wchar_t*)malloc((wideDir.length() + 1) * sizeof(wchar_t));
            if (Info->CurDir) {
                const wchar_t* src = wideDir.c_str();
                wchar_t* dst = const_cast<wchar_t*>(Info->CurDir);
                while (*src) *dst++ = *src++;
                *dst = L'\0';
            } else {
                Info->CurDir = L"/";
            }
            LIBMTP_destroy_file_t(objectFile);
        } else {
            Info->CurDir = L"/";
        }
    } else if (_currentStorageID != 0 && _mtpDevice) {
        LIBMTP_devicestorage_t* storage = _mtpDevice->GetDevice()->storage;
        while (storage) {
            if (storage->id == _currentStorageID) {
                std::string storageName = _mtpFileSystem->GetStorageDisplayName(storage);
                std::wstring wideStorage = StrMB2Wide(storageName);
                Info->CurDir = (wchar_t*)malloc((wideStorage.length() + 1) * sizeof(wchar_t));
                if (Info->CurDir) {
                    const wchar_t* src = wideStorage.c_str();
                    wchar_t* dst = const_cast<wchar_t*>(Info->CurDir);
                    while (*src) *dst++ = *src++;
                    *dst = L'\0';
                } else {
                    Info->CurDir = L"/";
                }
                break;
            }
            storage = storage->next;
        }
        if (!storage) {
            Info->CurDir = L"/";
        }
    } else if (_isConnected && _mtpDevice) {
        std::string deviceName = _mtpDevice->GetFriendlyName();
        if (deviceName.empty()) {
            deviceName = "MTP Device";
        }
        std::wstring wideDevice = StrMB2Wide(deviceName);
        Info->CurDir = (wchar_t*)malloc((wideDevice.length() + 1) * sizeof(wchar_t));
        if (Info->CurDir) {
            const wchar_t* src = wideDevice.c_str();
            wchar_t* dst = const_cast<wchar_t*>(Info->CurDir);
            while (*src) *dst++ = *src++;
            *dst = L'\0';
        } else {
            Info->CurDir = L"/";
        }
    } else {
        Info->CurDir = L"/";
    }
    
    Info->Format = L"MTP";
    _dynamicPanelTitle = GeneratePanelTitle();
    Info->PanelTitle = _dynamicPanelTitle.c_str();
    Info->InfoLines = nullptr;
    Info->DescrFiles = nullptr;
    Info->PanelModesArray = nullptr;
    Info->PanelModesNumber = 0;
    Info->StartPanelMode = 0;
    Info->StartSortMode = 0;
    Info->StartSortOrder = 0;
    Info->KeyBar = nullptr;
    Info->ShortcutData = nullptr;
}

int MTPPlugin::SetDirectory(const wchar_t *Dir, int OpMode)
{
    std::string dir = Dir ? StrWide2MB(Dir) : "";
    DBG("SetDirectory: Setting directory to: %s", dir.c_str());
    
    if (!_isConnected) {
        if (dir == ".." || dir == "/") {
            _currentStorageID = 0;
            _currentDirID = 0;
            wcscpy(_PanelTitle, L"MTP Devices");
            return TRUE;
        }
        return ByKey_TryEnterSelectedDevice() ? TRUE : FALSE;
    }
    
    try {
        if (dir == "..") {
            DBG("SetDirectory: Processing '..' navigation");
            if (_currentDirID == 0) {
                if (_currentStorageID != 0) {
                    _currentStorageID = 0;
                    _currentDirID = 0;
                    if (_mtpDevice) {
                        _mtpDevice->NavigateToRoot();
                    }
                    DBG("SetDirectory: Back to device root (showing storages)");
                    return TRUE;
                } else {
                    _isConnected = false;
                    _currentStorageID = 0;
                    _currentDirID = 0;
                    DBG("SetDirectory: Back to device selection");
                    return TRUE;
                }
            } else {
                LIBMTP_file_t* currentObject = LIBMTP_Get_Filemetadata(_mtpDevice->GetDevice(), _currentDirID);
                if (currentObject) {
                    uint32_t parentId = currentObject->parent_id;
                    LIBMTP_destroy_file_t(currentObject);
                    
                    if (parentId == 0) {
                        _currentDirID = 0;
                        if (_mtpDevice) {
                            std::string storageName = _mtpFileSystem->GetStorageName();
                            _mtpDevice->SetCurrentStorage(_currentStorageID, storageName);
                        }
                        _lastEnteredDirName.clear();
                        DBG("SetDirectory: Navigated to storage root");
                    } else {
                        _currentDirID = parentId;
                        if (_mtpDevice) {
                            LIBMTP_file_t* parentFile = LIBMTP_Get_Filemetadata(_mtpDevice->GetDevice(), parentId);
                            if (parentFile && parentFile->filename) {
                                std::string parentName = std::string(parentFile->filename);
                                _mtpDevice->SetCurrentDir(parentId, parentName);
                                LIBMTP_destroy_file_t(parentFile);
                            }
                        }
                        DBG("SetDirectory: Navigated to parent directory: ID=%u", parentId);
                    }
                    return TRUE;
                } else {
                    DBG("SetDirectory: Failed to get current object metadata");
                    return FALSE;
                }
            }
        }
        if (IsEncodedId(dir)) {
            DBG("SetDirectory: Navigating to encoded ID: %s", dir.c_str());
            SetCurrentFromEncodedId(dir);
            
            if (_currentDirID != 0) {
                LIBMTP_file_t* objectFile = LIBMTP_Get_Filemetadata(_mtpDevice->GetDevice(), _currentDirID);
                if (objectFile) {
                    if (objectFile->filetype == LIBMTP_FILETYPE_FOLDER) {
                        if (objectFile->filename) {
                            _lastEnteredDirName = std::string(objectFile->filename);
                        }
                        LIBMTP_destroy_file_t(objectFile);
                        DBG("SetDirectory: Successfully navigated to directory: ID=%u, Name='%s'", _currentDirID, _lastEnteredDirName.c_str());
                        return TRUE;
                    } else {
                        LIBMTP_destroy_file_t(objectFile);
                        DBG("SetDirectory: Object is not a directory: ID=%u", _currentDirID);
                        return FALSE;
                    }
                } else {
                    DBG("SetDirectory: Object not found: ID=%u", _currentDirID);
                    return FALSE;
                }
            } else if (_currentStorageID != 0) {
                _lastEnteredDirName.clear();
                DBG("SetDirectory: Successfully navigated to storage: ID=%u", _currentStorageID);
                return TRUE;
            } else {
                DBG("SetDirectory: Invalid encoded ID: %s", dir.c_str());
                return FALSE;
            }
        } else {
            DBG("SetDirectory: Looking for selected item with filename: %s", dir.c_str());
            intptr_t size = g_Info.Control(PANEL_ACTIVE, FCTL_GETSELECTEDPANELITEM, 0, 0);
            if (size >= (intptr_t)sizeof(PluginPanelItem)) {
                PluginPanelItem* item = (PluginPanelItem*)malloc(size + 0x100);
                if (item) {
                    memset(item, 0, size + 0x100);
                    g_Info.Control(PANEL_ACTIVE, FCTL_GETSELECTEDPANELITEM, 0, (LONG_PTR)(void*)item);
                    
                    std::string encodedId;
                    if (item->UserData != 0) {
                        char* encodedIdPtr = (char*)item->UserData;
                        encodedId = std::string(encodedIdPtr);
                        DBG("SetDirectory: Found encoded ID in UserData: %s", encodedId.c_str());
                    } else {
                        std::wstring wideDir(dir.c_str(), dir.c_str() + dir.length());
                        std::string cleanName = StrWide2MB(wideDir);
                        encodedId = _mtpFileSystem->GetEncodedIdForName(cleanName);
                        DBG("SetDirectory: Found encoded ID via shadow mechanism: %s -> %s", cleanName.c_str(), encodedId.c_str());
                    }
                    
                    if (!encodedId.empty()) {
                        _lastEnteredDirName = dir;
                        DBG("SetDirectory: Stored directory name for cursor restoration: %s", _lastEnteredDirName.c_str());
                        SetCurrentFromEncodedId(encodedId);
                        free(item);
                        return TRUE;
                    }
                    free(item);
                }
            }
            
            DBG("SetDirectory: Fallback - looking for object by filename: %s", dir.c_str());
            uint32_t objectId = _mtpFileSystem->FindObjectByFilename(dir);
            if (objectId != 0) {
                std::string encodedId = _mtpFileSystem->EncodeObjectId(objectId);
                DBG("SetDirectory: Found object ID %u, encoded as: %s", objectId, encodedId.c_str());
                _lastEnteredDirName = dir;
                DBG("SetDirectory: Stored directory name for cursor restoration: %s", _lastEnteredDirName.c_str());
                SetCurrentFromEncodedId(encodedId);
                return TRUE;
            }
        }
        return FALSE;
    } catch (const std::exception& e) {
        DBG("Exception in SetDirectory: %s", e.what());
        return FALSE;
    }
}

int MTPPlugin::ProcessKey(int Key, unsigned int ControlState)
{
    if (!_isConnected && Key == VK_RETURN && ControlState == 0) {
        return ByKey_TryEnterSelectedDevice() ? TRUE : FALSE;
    }
    return FALSE;
}

bool MTPPlugin::IsEncodedId(const std::string& str)
{
    if (str.length() != 9 || (str[0] != 'S' && str[0] != 'O')) {
        return false;
    }
    for (size_t i = 1; i < 9; i++) {
        if (!std::isxdigit(str[i])) {
            return false;
        }
    }
    try {
        uint32_t value = std::stoul(str.substr(1), nullptr, 16);
        return value != 0;
    } catch (const std::exception&) {
        return false;
    }
}

std::string MTPPlugin::GetCurrentEncodedId() const
{
    if (_currentDirID == 0) {
        // At storage root, return storage encoded ID
        if (_currentStorageID != 0) {
            return _mtpFileSystem->EncodeStorageId(_currentStorageID);
        } else {
            return ""; // At device root
        }
    } else {
        // In a directory, return object encoded ID
        return _mtpFileSystem->EncodeObjectId(_currentDirID);
    }
}

void MTPPlugin::SetCurrentFromEncodedId(const std::string& encodedId)
{
    if (encodedId.empty()) {
        _currentStorageID = 0;
        _currentDirID = 0;
        // Update MTPDevice to device root
        if (_mtpDevice) {
            _mtpDevice->NavigateToRoot();
        }
    } else if (encodedId[0] == 'S') {
        // Storage ID
        _currentStorageID = _mtpFileSystem->DecodeStorageId(encodedId);
        _currentDirID = 0; // At storage root
        // Update MTPDevice to storage root
        if (_mtpDevice) {
            std::string storageName = _mtpFileSystem->GetStorageName();
            _mtpDevice->SetCurrentStorage(_currentStorageID, storageName);
        }
    } else if (encodedId[0] == 'O') {
        // Object ID (directory)
        _currentDirID = _mtpFileSystem->DecodeObjectId(encodedId);
        // Find which storage this object belongs to
        _currentStorageID = _mtpFileSystem->FindStorageForObject(_currentDirID);
        // Update MTPDevice with directory info
        if (_mtpDevice) {
            // Get directory name from MTP
            LIBMTP_file_t* objectFile = LIBMTP_Get_Filemetadata(_mtpDevice->GetDevice(), _currentDirID);
            if (objectFile && objectFile->filename) {
                std::string dirName = std::string(objectFile->filename);
                _mtpDevice->SetCurrentDir(_currentDirID, dirName);
                LIBMTP_destroy_file_t(objectFile);
            }
        }
    }
}


int MTPPlugin::GetDeviceData(PluginPanelItem **pPanelItem, int *pItemsNumber)
{
    DBG("Getting device data...");
    LIBMTP_Init();
    
    LIBMTP_raw_device_t* rawdevices;
    int numrawdevices;
    LIBMTP_error_number_t err = LIBMTP_Detect_Raw_Devices(&rawdevices, &numrawdevices);
    
    if (err != LIBMTP_ERROR_NONE) {
        DBG("Failed to detect devices: %d - showing error message", err);
        *pItemsNumber = 1;
        *pPanelItem = (PluginPanelItem*)calloc(1, sizeof(PluginPanelItem));
        PluginPanelItem& item = (*pPanelItem)[0];
        item.FindData.lpwszFileName = (wchar_t*)calloc(50, sizeof(wchar_t));
        wcscpy((wchar_t*)item.FindData.lpwszFileName, L"MTP detection failed");
        item.FindData.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        item.FindData.nFileSize = 0;
        item.FindData.nPhysicalSize = 0;
        item.FindData.ftCreationTime = {0};
        item.FindData.ftLastAccessTime = {0};
        item.FindData.ftLastWriteTime = {0};
        return TRUE;
    }
    
    DBG("Found %d devices", numrawdevices);
    
    if (numrawdevices == 0) {
        DBG("No devices found - showing message");
        *pItemsNumber = 1;
        *pPanelItem = (PluginPanelItem*)calloc(1, sizeof(PluginPanelItem));
        PluginPanelItem& item = (*pPanelItem)[0];
        item.FindData.lpwszFileName = (wchar_t*)calloc(50, sizeof(wchar_t));
        wcscpy((wchar_t*)item.FindData.lpwszFileName, L"No MTP devices found");
        item.FindData.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        item.FindData.nFileSize = 0;
        item.FindData.nPhysicalSize = 0;
        item.FindData.ftCreationTime = {0};
        item.FindData.ftLastAccessTime = {0};
        item.FindData.ftLastWriteTime = {0};
        LIBMTP_FreeMemory(rawdevices);
        return TRUE;
    }
    
    *pItemsNumber = numrawdevices;
    *pPanelItem = (PluginPanelItem*)calloc(numrawdevices, sizeof(PluginPanelItem));
    
    for (int i = 0; i < numrawdevices; i++) {
        PluginPanelItem& item = (*pPanelItem)[i];
        std::string deviceId = std::to_string(rawdevices[i].bus_location) + "_" + std::to_string(rawdevices[i].devnum);
        DBG("Creating device item %d: ID='%s', bus=%d, dev=%d", i, deviceId.c_str(), rawdevices[i].bus_location, rawdevices[i].devnum);
        
        std::string friendlyName = GetDeviceFriendlyNameFromRawDevice(rawdevices[i]);
        DBG("GetDeviceData: Got friendly name: '%s'", friendlyName.c_str());
        
        std::string currentDeviceId = std::to_string(rawdevices[i].bus_location) + "_" + std::to_string(rawdevices[i].devnum);
        if (currentDeviceId == _deviceSerial && !_deviceName.empty()) {
            friendlyName = _deviceName;
            DBG("GetDeviceData: Using stored friendly name for previously connected device: '%s'", friendlyName.c_str());
        }
        
        if (friendlyName.empty()) {
            friendlyName = "Device " + std::to_string(i + 1);
            DBG("GetDeviceData: Using fallback name: '%s'", friendlyName.c_str());
        }
        
        std::wstring wideFriendlyName = StrMB2Wide(friendlyName);
        item.FindData.lpwszFileName = (wchar_t*)calloc(wideFriendlyName.length() + 1, sizeof(wchar_t));
        if (item.FindData.lpwszFileName) {
            const wchar_t* src = wideFriendlyName.c_str();
            wchar_t* dst = const_cast<wchar_t*>(item.FindData.lpwszFileName);
            while (*src) *dst++ = *src++;
            *dst = L'\0';
        }
        
        char* deviceIdPtr = (char*)malloc(deviceId.length() + 1);
        if (deviceIdPtr) {
            strcpy(deviceIdPtr, deviceId.c_str());
            item.UserData = (DWORD_PTR)deviceIdPtr;
            DBG("GetDeviceData: Stored device ID in UserData: '%s' (ptr=%p, UserData=%p)", deviceIdPtr, deviceIdPtr, (void*)item.UserData);
        } else {
            item.UserData = 0;
            DBG("GetDeviceData: Failed to allocate memory for device ID");
        }
        
        item.FindData.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        item.FindData.nFileSize = 0;
        item.FindData.nPhysicalSize = 0;
        item.FindData.ftCreationTime = {0};
        item.FindData.ftLastAccessTime = {0};
        item.FindData.ftLastWriteTime = {0};
    }
    
    LIBMTP_FreeMemory(rawdevices);
    DBG("Successfully created %d panel items", numrawdevices);
    DBG("GetDeviceData completed successfully");
    return TRUE;
}

int MTPPlugin::GetFileData(PluginPanelItem **pPanelItem, int *pItemsNumber)
{
    if (!_isConnected) {
        // Not connected - show device list
        return GetDeviceData(pPanelItem, pItemsNumber);
    }
    
    if (!_mtpFileSystem) {
        *pPanelItem = nullptr;
        *pItemsNumber = 0;
        return FALSE;
    }
    
    try {
        // Convert current numeric IDs to encoded ID for ListDirectory
        std::string currentEncodedId = GetCurrentEncodedId();
        DBG("GetFileData: Getting files for encoded ID: '%s' (storage=%u, dir=%u)", 
            currentEncodedId.c_str(), _currentStorageID, _currentDirID);
        
        std::vector<PluginPanelItem> files = _mtpFileSystem->ListDirectory(currentEncodedId);
        DBG("GetFileData: ListDirectory returned %zu files", files.size());
        
        if (files.empty()) {
            DBG("GetFileData: Empty directory - returning empty result (ADDDOTS will add '..')");
            *pPanelItem = nullptr;
            *pItemsNumber = 0;
            return TRUE;  // Return TRUE for empty directory - ADDDOTS will add ".."
        }
        
        *pItemsNumber = files.size();
        *pPanelItem = (PluginPanelItem*)calloc(files.size(), sizeof(PluginPanelItem));
        
        for (size_t i = 0; i < files.size(); i++) {
            (*pPanelItem)[i] = files[i];
        }
        
        return TRUE;
        
    } catch (const std::exception& e) {
        DBG("Exception in GetFileData: %s", e.what());
        *pPanelItem = nullptr;
        *pItemsNumber = 0;
        return FALSE;
    }
}

bool MTPPlugin::ByKey_TryEnterSelectedDevice()
{
    std::string deviceId = GetCurrentPanelItemDeviceName();
    if (deviceId.empty()) {
        DBG("No device selected");
        return false;
    }
    
    DBG("Connecting to selected device: %s", deviceId.c_str());
    wcscpy(_PanelTitle, L"Connecting to MTP device...");
    g_Info.Control(PANEL_ACTIVE, FCTL_UPDATEPANEL, 0, 0);
    
    bool connected = false;
    try {
        connected = ConnectToDevice(deviceId);
    } catch (const std::exception& e) {
        DBG("Exception during device connection: %s", e.what());
        connected = false;
    }
    
    if (!connected) {
        DBG("Failed to connect to device: %s", deviceId.c_str());
        const wchar_t* errorMsg = L"Failed to connect to MTP device.\nDevice may be busy or not responding.";
        g_Info.Message(g_Info.ModuleNumber, FMSG_MB_OK | FMSG_WARNING, nullptr, &errorMsg, 1, 0);
        return false;
    }
    
    _isConnected = true;
    _deviceSerial = deviceId;
    _currentStorageID = 0;
    _currentDirID = 0;
    
    std::wstring panel_title = L"MTP Device:/";
    wcscpy(_PanelTitle, panel_title.c_str());
    g_Info.Control(PANEL_ACTIVE, FCTL_UPDATEPANEL, 0, 0);
    
    PanelRedrawInfo ri = {};
    ri.CurrentItem = ri.TopPanelItem = 0;
    g_Info.Control(PANEL_ACTIVE, FCTL_REDRAWPANEL, 0, (LONG_PTR)&ri);
    
    DBG("Successfully connected to device: %s", deviceId.c_str());
    return true;
}

std::string MTPPlugin::GetDeviceFriendlyName(const std::string& deviceId)
{
    LIBMTP_Init();
    
    // Parse device ID to get bus and device number
    size_t underscore_pos = deviceId.find('_');
    if (underscore_pos == std::string::npos) {
        return "";
    }
    
    int bus_location = std::stoi(deviceId.substr(0, underscore_pos));
    int devnum = std::stoi(deviceId.substr(underscore_pos + 1));
    
    // Find the raw device
    LIBMTP_raw_device_t* rawdevices;
    int numrawdevices;
    LIBMTP_error_number_t err = LIBMTP_Detect_Raw_Devices(&rawdevices, &numrawdevices);
    
    if (err != LIBMTP_ERROR_NONE) {
        LIBMTP_FreeMemory(rawdevices);
        return "";
    }
    
    // Find matching device
    for (int i = 0; i < numrawdevices; i++) {
        if (rawdevices[i].bus_location == bus_location && rawdevices[i].devnum == devnum) {
            // Open device to get real name with timeout
            std::atomic<bool> device_opened{false};
            std::atomic<LIBMTP_mtpdevice_t*> device_result{nullptr};
            
            std::thread open_thread([&]() {
                device_result = LIBMTP_Open_Raw_Device(&rawdevices[i]);
                device_opened = true;
            });
            
            // Wait for device opening with timeout (2 seconds)
            auto start_time = std::chrono::steady_clock::now();
            while (!device_opened) {
                auto elapsed = std::chrono::steady_clock::now() - start_time;
                if (elapsed > std::chrono::seconds(2)) {
                    DBG("Device opening timeout for friendly name");
                    open_thread.detach();
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            
            LIBMTP_mtpdevice_t* device = nullptr;
            if (device_opened) {
                open_thread.join();
                device = device_result.load();
            }
            
            if (device) {
                char* manufacturer = LIBMTP_Get_Manufacturername(device);
                char* model = LIBMTP_Get_Modelname(device);
                
                std::string friendly_name;
                if (manufacturer && model) {
                    friendly_name = std::string(manufacturer) + " " + std::string(model);
                } else if (manufacturer) {
                    friendly_name = std::string(manufacturer);
                } else if (model) {
                    friendly_name = std::string(model);
                } else {
                    friendly_name = "";
                }
                
                if (manufacturer) free(manufacturer);
                if (model) free(model);
                
                LIBMTP_Release_Device(device);
                LIBMTP_FreeMemory(rawdevices);
                return friendly_name;
            } else {
                // Device opening failed or timed out
                LIBMTP_FreeMemory(rawdevices);
                return "";
            }
        }
    }
    
    LIBMTP_FreeMemory(rawdevices);
    return "";
}

std::string MTPPlugin::GetDeviceFriendlyNameFromRawDevice(const LIBMTP_raw_device_t& rawDevice)
{
    // Open the raw device to get MTP device information
    // Need to cast away const for the libmtp function
    LIBMTP_raw_device_t* nonConstRawDevice = const_cast<LIBMTP_raw_device_t*>(&rawDevice);
    DBG("GetDeviceFriendlyNameFromRawDevice: Attempting to open device bus=%d, dev=%d", rawDevice.bus_location, rawDevice.devnum);
    LIBMTP_mtpdevice_t* device = LIBMTP_Open_Raw_Device_Uncached(nonConstRawDevice);
    if (!device) {
        // Device can't be opened - return empty string
        DBG("GetDeviceFriendlyNameFromRawDevice: Failed to open device bus=%d, dev=%d", rawDevice.bus_location, rawDevice.devnum);
        return "";
    }
    
    std::string friendlyName;
    
    // Try to get friendly name first
    char* friendly = LIBMTP_Get_Friendlyname(device);
    if (friendly) {
        friendlyName = std::string(friendly);
        DBG("GetDeviceFriendlyNameFromRawDevice: Found friendly name: '%s'", friendlyName.c_str());
        free(friendly);
    } else {
        DBG("GetDeviceFriendlyNameFromRawDevice: No friendly name available, trying manufacturer/model");
        // If no friendly name, try to construct from manufacturer and model
        char* manufacturer = LIBMTP_Get_Manufacturername(device);
        char* model = LIBMTP_Get_Modelname(device);
        
        if (manufacturer && model) {
            friendlyName = std::string(manufacturer) + " " + std::string(model);
            DBG("GetDeviceFriendlyNameFromRawDevice: Using manufacturer+model: '%s'", friendlyName.c_str());
        } else if (manufacturer) {
            friendlyName = std::string(manufacturer);
            DBG("GetDeviceFriendlyNameFromRawDevice: Using manufacturer: '%s'", friendlyName.c_str());
        } else if (model) {
            friendlyName = std::string(model);
            DBG("GetDeviceFriendlyNameFromRawDevice: Using model: '%s'", friendlyName.c_str());
        } else {
            DBG("GetDeviceFriendlyNameFromRawDevice: No manufacturer or model available");
            // No device information available
            friendlyName = "";
        }
        
        if (manufacturer) free(manufacturer);
        if (model) free(model);
    }
    
    LIBMTP_Release_Device(device);
    return friendlyName;
}

std::string MTPPlugin::GetCurrentPanelItemDeviceName()
{
    intptr_t size = g_Info.Control(PANEL_ACTIVE, FCTL_GETSELECTEDPANELITEM, 0, 0);
    if (size < (intptr_t)sizeof(PluginPanelItem)) {
        DBG("No selected item or invalid size: %ld", (long)size);
        return "";
    }
    
    PluginPanelItem *item = (PluginPanelItem *)malloc(size + 0x100);
    if (!item) {
        DBG("Failed to allocate memory for panel item");
        return "";
    }
    
    memset(item, 0, size + 0x100);
    g_Info.Control(PANEL_ACTIVE, FCTL_GETSELECTEDPANELITEM, 0, (LONG_PTR)(void *)item);
    
    if (!item->UserData) {
        DBG("No UserData in selected item");
        free(item);
        return "";
    }
    
    char* deviceIdPtr = (char*)item->UserData;
    DBG("GetCurrentPanelItemDeviceName: UserData=%p, deviceIdPtr=%p", (void*)item->UserData, deviceIdPtr);
    std::string deviceId;
    if (deviceIdPtr) {
        deviceId = std::string(deviceIdPtr);
        DBG("Extracted device ID from UserData: '%s' (ptr=%p)", deviceId.c_str(), deviceIdPtr);
    } else {
        DBG("deviceIdPtr is NULL!");
        deviceId = "";
    }
    
    free(item);
    return deviceId;
}

std::wstring MTPPlugin::GeneratePanelTitle()
{
    if (!_isConnected) {
        // Device selection mode - show "MTP Devices"
        return L"MTP Devices";
    }
    
    if (!_mtpDevice) {
        return L"MTP Device";
    }
    
    // Use centralized state from MTPDevice
    std::string deviceName = _mtpDevice->GetFriendlyName();
    if (deviceName.empty()) {
        deviceName = "MTP Device";
    }
    
    std::string currentPath = _mtpDevice->GetCurrentPath();
    DBG("GeneratePanelTitle: deviceName='%s', currentPath='%s', storageID=%u, dirID=%u", 
        deviceName.c_str(), currentPath.c_str(), _currentStorageID, _currentDirID);
    
    if (_currentStorageID == 0) {
        // At device root (showing storages) - show device name
        return StrMB2Wide(deviceName);
    } else if (_currentDirID == 0) {
        // At storage root - show current path (which should be "/")
        return StrMB2Wide(currentPath);
    } else {
        // In a directory - show current path
        return StrMB2Wide(currentPath);
    }
}

bool MTPPlugin::ConnectToDevice(const std::string &deviceId)
{
    DBG("ConnectToDevice: Connecting to device: %s", deviceId.c_str());
    
    size_t underscore_pos = deviceId.find('_');
    if (underscore_pos == std::string::npos) {
        DBG("ConnectToDevice: Invalid device ID format: %s", deviceId.c_str());
        return false;
    }
    
    int bus_location = std::stoi(deviceId.substr(0, underscore_pos));
    int devnum = std::stoi(deviceId.substr(underscore_pos + 1));
    DBG("ConnectToDevice: Parsed bus=%d, dev=%d", bus_location, devnum);
    
    LIBMTP_Init();
    LIBMTP_raw_device_t* rawdevices;
    int numrawdevices;
    LIBMTP_error_number_t err = LIBMTP_Detect_Raw_Devices(&rawdevices, &numrawdevices);
    
    if (err != LIBMTP_ERROR_NONE) {
        DBG("ConnectToDevice: Error detecting devices: %d", err);
        LIBMTP_FreeMemory(rawdevices);
        return false;
    }
    
    for (int i = 0; i < numrawdevices; i++) {
        if (rawdevices[i].bus_location == bus_location && rawdevices[i].devnum == devnum) {
            _mtpDevice = std::make_unique<MTPDevice>(deviceId);
            if (_mtpDevice && _mtpDevice->Connect()) {
                _isConnected = true;
                _deviceSerial = deviceId;
                _mtpFileSystem = std::make_unique<MTPFileSystem>(std::shared_ptr<MTPDevice>(_mtpDevice.get(), [](MTPDevice*){}));
                _currentStorageID = 0;
                _currentDirID = 0;
                
                _deviceName = _mtpDevice->GetFriendlyName();
                if (_deviceName.empty()) {
                    _deviceName = "Device " + std::to_string(i + 1);
                }
                DBG("ConnectToDevice: Using device friendly name: '%s'", _deviceName.c_str());
                DBG("ConnectToDevice: Successfully connected to device: %s", _deviceName.c_str());
                LIBMTP_FreeMemory(rawdevices);
                return true;
            }
        }
    }
    
    DBG("ConnectToDevice: Could not find or connect to device: %s", deviceId.c_str());
    LIBMTP_FreeMemory(rawdevices);
    return false;
}

PluginStartupInfo *MTPPlugin::GetInfo()
{
    return &g_Info;
}

int MTPPlugin::MakeDirectory(const wchar_t **Name, int OpMode)
{
    if (!_isConnected || !_mtpDevice) {
        return FALSE;
    }
    
    std::string dir_name;
    if (Name && *Name) {
        dir_name = StrWide2MB(*Name);
    }
    
    if (!(OpMode & OPM_SILENT)) {
        if (!MTPDialogs::AskCreateDirectory(dir_name)) {
            return -1;
        }
    }
    
    if (dir_name.empty()) {
        return FALSE;
    }
    
    int result = _mtpDevice->CreateMTPDirectory(dir_name);
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

int MTPPlugin::DeleteFiles(PluginPanelItem *PanelItem, int ItemsNumber, int OpMode)
{
    if (ItemsNumber <= 0 || !_isConnected || !_mtpDevice || !PanelItem) {
        return FALSE;
    }
    
    // Check for special directory entries that cannot be deleted
    for (int i = 0; i < ItemsNumber; i++) {
        if (PanelItem[i].FindData.lpwszFileName) {
            if (wcscmp(PanelItem[i].FindData.lpwszFileName, L"..") == 0) {
                DBG("DeleteFiles: Cannot delete parent directory '..'");
                WINPORT(SetLastError)(EACCES); // Access denied
                return FALSE;
            }
            if (wcscmp(PanelItem[i].FindData.lpwszFileName, L".") == 0) {
                DBG("DeleteFiles: Cannot delete current directory '.'");
                WINPORT(SetLastError)(EACCES); // Access denied
                return FALSE;
            }
        }
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
            result = MTPDialogs::Message(FMSG_MB_YESNO,
                L"Delete",
                L"Do you wish to delete",
                itemType,
                itemName);
        } else {
            result = MTPDialogs::Message(FMSG_MB_YESNO,
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
                redResult = MTPDialogs::Message(FMSG_WARNING | FMSG_MB_YESNO,
                    L"Delete files",
                    L"Do you wish to delete",
                    std::to_wstring(ItemsNumber) + L" items");
            } else if (hasNonEmptyDirs && ItemsNumber == 1) {
                redResult = MTPDialogs::Message(FMSG_WARNING | FMSG_MB_YESNO,
                    L"Delete folder",
                    L"The following folder will be deleted:",
                    L"/" + std::wstring(PanelItem[0].FindData.lpwszFileName));
            } else if (hasNonEmptyDirs && ItemsNumber > 1) {
                if (fileCount > 0 && folderCount > 0) {
                    redResult = MTPDialogs::Message(FMSG_WARNING | FMSG_MB_YESNO,
                        L"Delete items",
                        L"The following items will be deleted:",
                        std::to_wstring(folderCount) + L" folders",
                        std::to_wstring(fileCount) + L" files");
                } else {
                    redResult = MTPDialogs::Message(FMSG_WARNING | FMSG_MB_YESNO,
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
        // Get object ID from UserData
        uint32_t objectId = 0;
        if (PanelItem[i].UserData != 0) {
            char* encodedIdPtr = (char*)PanelItem[i].UserData;
            if (encodedIdPtr) {
                std::string encodedId = std::string(encodedIdPtr);
                if (encodedId[0] == 'O') {
                    objectId = _mtpFileSystem->DecodeObjectId(encodedId);
                }
            }
        }
        
        if (objectId == 0) {
            DBG("DeleteFiles: Could not get object ID for item %d", i);
            lastErrorCode = EINVAL;
            continue;
        }
        
        int result = 0;
        if (PanelItem[i].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            result = _mtpDevice->DeleteMTPDirectory(objectId);
        } else {
            result = _mtpDevice->DeleteMTPFile(objectId);
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

int MTPPlugin::GetFiles(PluginPanelItem *PanelItem, int ItemsNumber, int Move, const wchar_t **DestPath, int OpMode)
{
    if (ItemsNumber <= 0 || !_isConnected || !_mtpDevice || !PanelItem) {
        DBG("GetFiles: Invalid parameters");
        return FALSE;
    }
    
    DBG("GetFiles: Processing %d items, Move=%d, OpMode=0x%x", ItemsNumber, Move, OpMode);
    
    // Handle F3 View operation (OPM_VIEW)
    if (OpMode & OPM_VIEW) {
        DBG("GetFiles: F3 View operation detected");
        if (ItemsNumber > 0) {
            std::string fileName = PanelItem[0].FindData.lpwszFileName ? 
                                  StrWide2MB(PanelItem[0].FindData.lpwszFileName) : "unknown_file";
            
            if (PanelItem[0].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                DBG("GetFiles: Cannot view directory: %s", fileName.c_str());
                return FALSE;
            }
            
            // Get object ID from UserData
            uint32_t objectId = 0;
            if (PanelItem[0].UserData != 0) {
                char* encodedIdPtr = (char*)PanelItem[0].UserData;
                if (encodedIdPtr) {
                    std::string encodedId = std::string(encodedIdPtr);
                    if (encodedId[0] == 'O') {
                        objectId = _mtpFileSystem->DecodeObjectId(encodedId);
                    }
                }
            }
            
            if (objectId == 0) {
                DBG("GetFiles: Could not get object ID for viewing");
                return FALSE;
            }
            
            // Check if it's a file (not a directory)
            if (!_mtpDevice->IsFile(objectId)) {
                DBG("GetFiles: Object is not a file");
                return FALSE;
            }
            
            // Get file size to determine if we can view it
            uint64_t fileSize = 0;
            int sizeResult = _mtpDevice->GetMTPFileSize(objectId, fileSize);
            if (sizeResult != 0) {
                DBG("GetFiles: Could not get file size");
                return FALSE;
            }
            
            // Limit file size for viewing (e.g., 10MB max)
            const uint64_t MAX_VIEW_SIZE = 10 * 1024 * 1024; // 10MB
            if (fileSize > MAX_VIEW_SIZE) {
                const wchar_t* errorMsg = L"File is too large to view.\nMaximum viewable size is 10MB.";
                g_Info.Message(g_Info.ModuleNumber, FMSG_MB_OK | FMSG_WARNING, nullptr, &errorMsg, 1, 0);
                return FALSE;
            }
            
            DBG("GetFiles: Viewing file %s (size: %llu bytes)", fileName.c_str(), (unsigned long long)fileSize);
            
            // Use the temporary directory provided by far2l
            if (!DestPath || !DestPath[0]) {
                DBG("GetFiles: No destination path provided for viewing");
                return FALSE;
            }
            
            std::string destPath = StrWide2MB(DestPath[0]);
            std::string tempPath = destPath;
            if (tempPath.back() != '/' && tempPath.back() != '\\') {
                tempPath += "/";
            }
            tempPath += fileName; // Use original filename, not prefixed
            
            DBG("GetFiles: Downloading file to temporary path: %s", tempPath.c_str());
            
            // Download file to temporary location
            int downloadResult = _mtpDevice->DownloadFile(objectId, tempPath);
            if (downloadResult != 0) {
                DBG("GetFiles: Failed to download file for viewing, error: %d", downloadResult);
                return FALSE;
            }
            
            DBG("GetFiles: Successfully downloaded file for viewing: %s", tempPath.c_str());
            return TRUE; // File is now available for viewing at tempPath
        }
        return FALSE;
    }
    
    // Handle F5/F6 Copy/Move operations
    if (!DestPath) {
        DBG("GetFiles: No destination path specified");
        return FALSE;
    }
    
    std::string destPath = StrWide2MB(DestPath[0]);
    if (destPath.empty()) {
        DBG("GetFiles: Empty destination path");
        return FALSE;
    }
    
    // Count files (skip directories)
    int fileCount = 0;
    for (int i = 0; i < ItemsNumber; i++) {
        if (!(PanelItem[i].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            fileCount++;
        }
    }
    
    if (fileCount == 0) {
        DBG("GetFiles: No files to download");
        return FALSE;
    }
    
    // Show confirmation dialog if not in silent mode
    if (!(OpMode & OPM_SILENT)) {
        std::wstring operation = Move ? L"Move" : L"Copy";
        std::wstring source = L"MTP Device";
        std::wstring destination = StrMB2Wide(destPath);
        
        if (!MTPDialogs::AskTransferConfirmation(operation.c_str(), source.c_str(), 
                                                destination.c_str(), fileCount)) {
            DBG("GetFiles: User cancelled transfer");
            return -1;
        }
    }
    
    int successCount = 0;
    int lastErrorCode = 0;
    int currentFile = 0;
    
    for (int i = 0; i < ItemsNumber; i++) {
        // Skip directories for file download
        if (PanelItem[i].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            DBG("GetFiles: Skipping directory: %s", 
                PanelItem[i].FindData.lpwszFileName ? StrWide2MB(PanelItem[i].FindData.lpwszFileName).c_str() : "Unknown");
            continue;
        }
        
        currentFile++;
        
        // Show progress dialog
        if (!(OpMode & OPM_SILENT)) {
            std::string fileName = PanelItem[i].FindData.lpwszFileName ? 
                                  StrWide2MB(PanelItem[i].FindData.lpwszFileName) : "unknown_file";
            std::wstring progressMsg = L"Downloading: " + StrMB2Wide(fileName);
            bool cancelled = false;
            MTPDialogs::ShowProgressDialog(L"MTP Download", progressMsg.c_str(), 
                                          currentFile, fileCount, cancelled);
            if (cancelled) {
                DBG("GetFiles: User cancelled transfer");
                break;
            }
        }
        
        // Get object ID from UserData
        uint32_t objectId = 0;
        if (PanelItem[i].UserData != 0) {
            char* encodedIdPtr = (char*)PanelItem[i].UserData;
            if (encodedIdPtr) {
                std::string encodedId = std::string(encodedIdPtr);
                if (encodedId[0] == 'O') {
                    objectId = _mtpFileSystem->DecodeObjectId(encodedId);
                }
            }
        }
        
        if (objectId == 0) {
            DBG("GetFiles: Could not get object ID for item %d", i);
            lastErrorCode = EINVAL;
            continue;
        }
        
        // Get filename
        std::string fileName = _mtpDevice->GetFileName(objectId);
        if (fileName.empty()) {
            fileName = PanelItem[i].FindData.lpwszFileName ? 
                      StrWide2MB(PanelItem[i].FindData.lpwszFileName) : "unknown_file";
        }
        
        // Construct full destination path
        std::string fullDestPath = destPath;
        if (fullDestPath.back() != '/' && fullDestPath.back() != '\\') {
            fullDestPath += "/";
        }
        fullDestPath += fileName;
        
        DBG("GetFiles: Downloading %s to %s", fileName.c_str(), fullDestPath.c_str());
        
        // Download the file
        int result = _mtpDevice->DownloadFile(objectId, fullDestPath);
        
        if (result == 0) {
            successCount++;
            DBG("GetFiles: Successfully downloaded %s", fileName.c_str());
            
            // If this is a move operation, delete the source file
            if (Move) {
                DBG("GetFiles: Move operation - deleting source file %s", fileName.c_str());
                int deleteResult = _mtpDevice->DeleteMTPFile(objectId);
                if (deleteResult != 0) {
                    DBG("GetFiles: Warning - failed to delete source file after move: %d", deleteResult);
                }
            }
        } else {
            lastErrorCode = result;
            DBG("GetFiles: Failed to download %s, error: %d", fileName.c_str(), result);
        }
    }
    
    if (successCount == 0) {
        WINPORT(SetLastError)(lastErrorCode);
    }
    
    DBG("GetFiles: Completed - %d/%d files processed successfully", successCount, fileCount);
    return (successCount > 0) ? TRUE : FALSE;
}

int MTPPlugin::PutFiles(PluginPanelItem *PanelItem, int ItemsNumber, int Move, const wchar_t *SrcPath, int OpMode)
{
    if (ItemsNumber <= 0 || !_isConnected || !_mtpDevice || !PanelItem || !SrcPath) {
        DBG("PutFiles: Invalid parameters");
        return FALSE;
    }
    
    DBG("PutFiles: Processing %d items, Move=%d", ItemsNumber, Move);
    
    // Get source path
    std::string srcPath = StrWide2MB(SrcPath);
    if (srcPath.empty()) {
        DBG("PutFiles: No source path specified");
        return FALSE;
    }
    
    // Count files (skip directories)
    int fileCount = 0;
    for (int i = 0; i < ItemsNumber; i++) {
        if (!(PanelItem[i].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            fileCount++;
        }
    }
    
    if (fileCount == 0) {
        DBG("PutFiles: No files to upload");
        return FALSE;
    }
    
    // Show confirmation dialog if not in silent mode
    if (!(OpMode & OPM_SILENT)) {
        std::wstring operation = Move ? L"Move" : L"Copy";
        std::wstring source = StrMB2Wide(srcPath);
        std::wstring destination = L"MTP Device";
        
        if (!MTPDialogs::AskTransferConfirmation(operation.c_str(), source.c_str(), 
                                                destination.c_str(), fileCount)) {
            DBG("PutFiles: User cancelled transfer");
            return -1;
        }
    }
    
    int successCount = 0;
    int lastErrorCode = 0;
    int currentFile = 0;
    
    for (int i = 0; i < ItemsNumber; i++) {
        // Skip directories for file upload
        if (PanelItem[i].FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            DBG("PutFiles: Skipping directory: %s", 
                PanelItem[i].FindData.lpwszFileName ? StrWide2MB(PanelItem[i].FindData.lpwszFileName).c_str() : "Unknown");
            continue;
        }
        
        currentFile++;
        
        // Show progress dialog
        if (!(OpMode & OPM_SILENT)) {
            std::string fileName = PanelItem[i].FindData.lpwszFileName ? 
                                  StrWide2MB(PanelItem[i].FindData.lpwszFileName) : "unknown_file";
            std::wstring progressMsg = L"Uploading: " + StrMB2Wide(fileName);
            bool cancelled = false;
            MTPDialogs::ShowProgressDialog(L"MTP Upload", progressMsg.c_str(), 
                                          currentFile, fileCount, cancelled);
            if (cancelled) {
                DBG("PutFiles: User cancelled transfer");
                break;
            }
        }
        
        // Get filename
        std::string fileName = PanelItem[i].FindData.lpwszFileName ? 
                              StrWide2MB(PanelItem[i].FindData.lpwszFileName) : "unknown_file";
        
        // Construct full source path
        std::string fullSrcPath = srcPath;
        if (fullSrcPath.back() != '/' && fullSrcPath.back() != '\\') {
            fullSrcPath += "/";
        }
        fullSrcPath += fileName;
        
        DBG("PutFiles: Uploading %s from %s", fileName.c_str(), fullSrcPath.c_str());
        
        // Upload the file
        int result = _mtpDevice->UploadFile(fullSrcPath, fileName);
        
        if (result == 0) {
            successCount++;
            DBG("PutFiles: Successfully uploaded %s", fileName.c_str());
            
            // If this is a move operation, delete the source file
            if (Move) {
                DBG("PutFiles: Move operation - deleting source file %s", fullSrcPath.c_str());
                if (remove(fullSrcPath.c_str()) != 0) {
                    DBG("PutFiles: Warning - failed to delete source file after move");
                }
            }
        } else {
            lastErrorCode = result;
            DBG("PutFiles: Failed to upload %s, error: %d", fileName.c_str(), result);
        }
    }
    
    if (successCount == 0) {
        WINPORT(SetLastError)(lastErrorCode);
    }
    
    DBG("PutFiles: Completed - %d/%d files processed successfully", successCount, fileCount);
    return (successCount > 0) ? TRUE : FALSE;
}


