/**
 * @file MTPDevice.cpp
 * @brief MTP Device Management Implementation
 * 
 * This file implements the MTPDevice class that handles MTP device
 * connection, file operations, and device management.
 * 
 * @author MTP Plugin Team
 * @version 1.0
 */

#include "MTPDevice.h"
#include "MTPLog.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <WideMB.h>
#include <cstring>
#include <cstdlib>

MTPDevice::MTPDevice(const std::string &device_id)
    : _device_id(device_id)
    , _current_path("/")
    , _device(nullptr)
    , _storage(nullptr)
    , _connected(false)
    , _currentStorageId(0)
    , _currentDirId(0)
    , _currentPath("/")
{
    DBG("MTPDevice created for device: %s", device_id.c_str());
}

MTPDevice::~MTPDevice()
{
    Disconnect();
    DBG("MTPDevice destroyed for device: %s", _device_id.c_str());
}

bool MTPDevice::InitializeMTP()
{
    DBG("Initializing MTP library");
    LIBMTP_Init();
    return true;
}

void MTPDevice::CleanupMTP()
{
    DBG("Cleaning up MTP library");
    if (_device) {
        LIBMTP_Release_Device(_device);
        _device = nullptr;
    }
}

bool MTPDevice::Connect()
{
    DBG("Attempting to connect to MTP device: %s", _device_id.c_str());
    
    if (!InitializeMTP()) {
        DBG("Failed to initialize MTP library");
        return false;
    }
    
    // Detect raw devices
    LIBMTP_raw_device_t* rawdevices;
    int numrawdevices;
    LIBMTP_error_number_t err = LIBMTP_Detect_Raw_Devices(&rawdevices, &numrawdevices);
    
    if (err != LIBMTP_ERROR_NONE) {
        DBG("Failed to detect MTP devices: %d", err);
        return false;
    }
    
    if (numrawdevices == 0) {
        DBG("No MTP devices found");
        LIBMTP_FreeMemory(rawdevices);
        return false;
    }
    
    // Try to connect to the specific device if device_id is provided
    LIBMTP_raw_device_t* targetDevice = nullptr;
    if (!_device_id.empty()) {
        // Parse device ID to find the specific device
        size_t underscore_pos = _device_id.find('_');
        if (underscore_pos != std::string::npos) {
            int bus_location = std::stoi(_device_id.substr(0, underscore_pos));
            int devnum = std::stoi(_device_id.substr(underscore_pos + 1));
            
            for (int i = 0; i < numrawdevices; i++) {
                if (rawdevices[i].bus_location == bus_location && rawdevices[i].devnum == devnum) {
                    targetDevice = &rawdevices[i];
                    break;
                }
            }
        }
    }
    
    // If no specific device found or no device_id, use first available
    if (!targetDevice) {
        targetDevice = &rawdevices[0];
    }
    
    DBG("Attempting to open MTP device (bus: %d, dev: %d)", targetDevice->bus_location, targetDevice->devnum);
    
    // Try to connect with a timeout approach
    _device = LIBMTP_Open_Raw_Device_Uncached(targetDevice);
    LIBMTP_FreeMemory(rawdevices);
    
    if (!_device) {
        DBG("Failed to open MTP device - device may be busy or not responding");
        return false;
    }
    
    // Dump and clear error stack
    LIBMTP_Dump_Errorstack(_device);
    LIBMTP_Clear_Errorstack(_device);
    
    // Try to get storage information (optional - device may not have storages)
    DBG("Attempting to get storage information...");
    
    // Use a timeout approach for storage retrieval
    std::atomic<bool> storage_retrieved{false};
    std::atomic<int> storage_result{0};
    std::thread storage_thread([&]() {
        storage_result = LIBMTP_Get_Storage(_device, LIBMTP_STORAGE_SORTBY_NOTSORTED);
        storage_retrieved = true;
    });
    
    // Wait for storage retrieval with timeout (3 seconds)
    auto start_time = std::chrono::steady_clock::now();
    while (!storage_retrieved) {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (elapsed > std::chrono::seconds(3)) {
            DBG("Storage retrieval timeout - continuing without storage");
            storage_thread.detach();
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Handle storage result
    if (storage_retrieved) {
        storage_thread.join();
        int ret = storage_result.load();
        if (ret == 0) {
            _storage = _device->storage;
            if (_storage) {
                DBG("Found storage: %s", _storage->StorageDescription ? _storage->StorageDescription : "Unknown");
            } else {
                DBG("No storage available - device may be unauthorized or empty");
            }
        } else {
            DBG("Storage retrieval failed (error: %d) - device may be unauthorized", ret);
            LIBMTP_Dump_Errorstack(_device);
            LIBMTP_Clear_Errorstack(_device);
        }
    }
    
    // Always set storage to nullptr initially - will be set above if available
    if (!_storage) {
        _storage = nullptr;
        DBG("Connected without storage - device may need authorization or have no storage");
    }
    
    _connected = true;
    
    // Get device properties from the connected device
    char* friendly = LIBMTP_Get_Friendlyname(_device);
    if (friendly) {
        _friendlyName = std::string(friendly);
        free(friendly);
        DBG("Device friendly name: '%s'", _friendlyName.c_str());
    } else {
        DBG("No friendly name available for device");
    }
    
    DBG("Device connected successfully");
    
    DBG("Successfully connected to MTP device");
    return true;
}

void MTPDevice::Disconnect()
{
    if (_connected) {
        DBG("Disconnecting from MTP device");
        CleanupMTP();
        _connected = false;
        _storage = nullptr;
    }
}

// These methods are already defined inline in the header

// Removed RunMTPCommand - not used

// Removed GetCurrentWorkingDirectory - not used

// Removed DirectoryEnum - handled by MTPFileSystem

// Removed SetDirectory - handled by MTPFileSystem

// File operations removed - navigation only

// Removed WStringToString - not used

int MTPDevice::Str2Errno(const std::string &mtpError)
{
    // Map MTP errors to errno values
    if (mtpError.find("not found") != std::string::npos) return ENOENT;
    if (mtpError.find("permission") != std::string::npos) return EACCES;
    if (mtpError.find("busy") != std::string::npos) return EBUSY;
    if (mtpError.find("no space") != std::string::npos) return ENOSPC;
    return EIO;
}

void MTPDevice::SetCurrentStorage(uint32_t storageId, const std::string& storageName)
{
    _currentStorageId = storageId;
    _currentDirId = 0;  // Reset to storage root
    _currentPath = "/";  // Start from root without storage name
    DBG("SetCurrentStorage: ID=%u, Name='%s', Path='%s'", storageId, storageName.c_str(), _currentPath.c_str());
}

void MTPDevice::SetCurrentDir(uint32_t dirId, const std::string& dirName)
{
    _currentDirId = dirId;
    if (!_currentPath.empty() && _currentPath.back() == '/') {
        _currentPath += dirName + "/";
    } else {
        _currentPath = "/" + dirName + "/";
    }
    DBG("SetCurrentDirectory: ID=%u, Name='%s', Path='%s'", dirId, dirName.c_str(), _currentPath.c_str());
}

void MTPDevice::NavigateUp()
{
    if (_currentDirId != 0) {
        // We're in a directory, go up to parent
        _currentDirId = 0;  // Go back to storage root
        // Remove last directory from path
        size_t lastSlash = _currentPath.find_last_of('/', _currentPath.length() - 2);
        if (lastSlash != std::string::npos) {
            _currentPath = _currentPath.substr(0, lastSlash + 1);
        }
        DBG("NavigateUp: Back to storage root, Path='%s'", _currentPath.c_str());
    } else if (_currentStorageId != 0) {
        // We're in storage root, go back to device root
        _currentStorageId = 0;
        _currentPath = "/";
        DBG("NavigateUp: Back to device root, Path='%s'", _currentPath.c_str());
    }
}

void MTPDevice::NavigateToRoot()
{
    _currentStorageId = 0;
    _currentDirId = 0;
    _currentPath = "/";
    DBG("NavigateToRoot: Path='%s'", _currentPath.c_str());
}

// These methods are already defined inline in the header
