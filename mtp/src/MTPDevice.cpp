#include "MTPDevice.h"
#include "MTPLog.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <WideMB.h>
#include <cstring>
#include <cstdlib>
#include <utils.h>
#include <farplug-wide.h>

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

int MTPDevice::Str2Errno(const std::string &mtpError)
{
    if (mtpError.find("not found") != std::string::npos) return ENOENT;
    if (mtpError.find("permission") != std::string::npos) return EACCES;
    if (mtpError.find("busy") != std::string::npos) return EBUSY;
    if (mtpError.find("no space") != std::string::npos) return ENOSPC;
    return EIO;
}

void MTPDevice::SetCurrentStorage(uint32_t storageId, const std::string& storageName)
{
    _currentStorageId = storageId;
    _currentDirId = 0;
    _currentPath = "/";
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

int MTPDevice::CreateMTPDirectory(const std::string& dirName)
{
    if (!_connected || !_device) {
        DBG("CreateDirectory: Device not connected");
        return EIO;
    }
    
    if (dirName.empty()) {
        DBG("CreateDirectory: Empty directory name");
        return EINVAL;
    }
    
    // Determine parent object ID
    uint32_t parentId = LIBMTP_FILES_AND_FOLDERS_ROOT;
    if (_currentDirId != 0) {
        parentId = _currentDirId;
    }
    
    // Determine storage ID
    uint32_t storageId = 0;
    if (_currentStorageId != 0) {
        storageId = _currentStorageId;
    } else if (_storage) {
        storageId = _storage->id;
    } else {
        DBG("CreateDirectory: No storage available");
        return EIO;
    }
    
    DBG("CreateDirectory: Creating '%s' in storage %u, parent %u", dirName.c_str(), storageId, parentId);
    
    // Use the correct libmtp function for creating folders
    uint32_t result = LIBMTP_Create_Folder(_device, const_cast<char*>(dirName.c_str()), parentId, storageId);
    
    if (result != 0) {
        DBG("CreateDirectory: Successfully created directory '%s' with ID %u", dirName.c_str(), result);
        return 0;
    } else {
        DBG("CreateDirectory: Failed to create directory '%s'", dirName.c_str());
        return EIO;
    }
}

int MTPDevice::DeleteMTPFile(uint32_t objectId)
{
    if (!_connected || !_device) {
        DBG("DeleteFile: Device not connected");
        return EIO;
    }
    
    if (objectId == 0) {
        DBG("DeleteFile: Invalid object ID");
        return EINVAL;
    }
    
    DBG("DeleteFile: Deleting file with ID %u", objectId);
    
    int result = LIBMTP_Delete_Object(_device, objectId);
    
    if (result == 0) {
        DBG("DeleteFile: Successfully deleted file with ID %u", objectId);
        return 0;
    } else {
        DBG("DeleteFile: Failed to delete file with ID %u, error: %d", objectId, result);
        return EIO;
    }
}

int MTPDevice::DeleteMTPDirectory(uint32_t objectId)
{
    if (!_connected || !_device) {
        DBG("DeleteDirectory: Device not connected");
        return EIO;
    }
    
    if (objectId == 0) {
        DBG("DeleteDirectory: Invalid object ID");
        return EINVAL;
    }
    
    DBG("DeleteDirectory: Deleting directory with ID %u", objectId);
    
    // For directories, we also use LIBMTP_Delete_Object
    // libmtp should handle recursive deletion if the device supports it
    int result = LIBMTP_Delete_Object(_device, objectId);
    
    if (result == 0) {
        DBG("DeleteDirectory: Successfully deleted directory with ID %u", objectId);
        return 0;
    } else {
        DBG("DeleteDirectory: Failed to delete directory with ID %u, error: %d", objectId, result);
        return EIO;
    }
}

int MTPDevice::DownloadFile(uint32_t objectId, const std::string& localPath)
{
    if (!_connected || !_device) {
        DBG("DownloadFile: Device not connected");
        return EIO;
    }
    
    if (objectId == 0) {
        DBG("DownloadFile: Invalid object ID");
        return EINVAL;
    }
    
    DBG("DownloadFile: Downloading file ID %u to %s", objectId, localPath.c_str());
    
    // Get file metadata first
    LIBMTP_file_t* file = LIBMTP_Get_Filemetadata(_device, objectId);
    if (!file) {
        DBG("DownloadFile: Could not get file metadata for ID %u", objectId);
        return ENOENT;
    }
    
    // Check if it's actually a file (not a directory)
    if (file->filetype == LIBMTP_FILETYPE_FOLDER) {
        DBG("DownloadFile: Object %u is a directory, not a file", objectId);
        LIBMTP_destroy_file_t(file);
        return EISDIR;
    }
    
    // Store file size before destroying the file metadata
    uint64_t expectedFileSize = file->filesize;
    
    // Download the file
    int result = LIBMTP_Get_File_To_File(_device, objectId, localPath.c_str(), nullptr, nullptr);
    
    LIBMTP_destroy_file_t(file);
    
    if (result == 0) {
        // Verify the file was actually created and has the expected size
        struct stat fileStat;
        if (stat(localPath.c_str(), &fileStat) == 0) {
            if (fileStat.st_size == expectedFileSize) {
                DBG("DownloadFile: Successfully downloaded file ID %u to %s (size: %lld bytes)", 
                    objectId, localPath.c_str(), (long long)fileStat.st_size);
                return 0;
            } else {
                DBG("DownloadFile: File size mismatch - expected %llu bytes, got %lld bytes", 
                    (unsigned long long)expectedFileSize, (long long)fileStat.st_size);
                // Remove the incomplete file
                remove(localPath.c_str());
                return EIO;
            }
        } else {
            DBG("DownloadFile: Downloaded file ID %u but could not stat the file", objectId);
            return EIO;
        }
    } else {
        DBG("DownloadFile: Failed to download file ID %u, error: %d", objectId, result);
        return EIO;
    }
}

int MTPDevice::UploadFile(const std::string& localPath, const std::string& remoteName, uint32_t parentId)
{
    if (!_connected || !_device) {
        DBG("UploadFile: Device not connected");
        return EIO;
    }
    
    if (localPath.empty() || remoteName.empty()) {
        DBG("UploadFile: Invalid parameters");
        return EINVAL;
    }
    
    // Determine parent object ID
    if (parentId == 0) {
        if (_currentDirId != 0) {
            parentId = _currentDirId;
        } else {
            parentId = LIBMTP_FILES_AND_FOLDERS_ROOT;
        }
    }
    
    // Determine storage ID
    uint32_t storageId = 0;
    if (_currentStorageId != 0) {
        storageId = _currentStorageId;
    } else if (_storage) {
        storageId = _storage->id;
    } else {
        DBG("UploadFile: No storage available");
        return EIO;
    }
    
    // Verify source file exists and get its size
    struct stat sourceStat;
    if (stat(localPath.c_str(), &sourceStat) != 0) {
        DBG("UploadFile: Source file does not exist: %s", localPath.c_str());
        return ENOENT;
    }
    
    if (sourceStat.st_size == 0) {
        DBG("UploadFile: Source file is empty: %s", localPath.c_str());
        return EINVAL;
    }
    
    DBG("UploadFile: Uploading %s as %s to storage %u, parent %u (size: %lld bytes)", 
        localPath.c_str(), remoteName.c_str(), storageId, parentId, (long long)sourceStat.st_size);
    
    // Create file metadata structure for upload
    LIBMTP_file_t file_metadata;
    memset(&file_metadata, 0, sizeof(file_metadata));
    file_metadata.filename = const_cast<char*>(remoteName.c_str());
    file_metadata.filesize = sourceStat.st_size; // Set actual file size
    file_metadata.parent_id = parentId;
    file_metadata.storage_id = storageId;
    file_metadata.filetype = LIBMTP_FILETYPE_UNKNOWN; // Will be determined by libmtp
    
    // Upload the file
    int result = LIBMTP_Send_File_From_File(_device, localPath.c_str(), 
                                           &file_metadata, nullptr, nullptr);
    
    if (result == 0) {
        DBG("UploadFile: Successfully uploaded %s as %s", localPath.c_str(), remoteName.c_str());
        return 0;
    } else {
        DBG("UploadFile: Failed to upload %s, error: %d", localPath.c_str(), result);
        return EIO;
    }
}

int MTPDevice::GetFileContent(uint32_t objectId, void* buffer, size_t bufferSize, size_t& bytesRead)
{
    if (!_connected || !_device) {
        DBG("GetFileContent: Device not connected");
        return EIO;
    }
    
    if (objectId == 0 || !buffer || bufferSize == 0) {
        DBG("GetFileContent: Invalid parameters");
        return EINVAL;
    }
    
    DBG("GetFileContent: Getting content for file ID %u, buffer size %zu", objectId, bufferSize);
    
    // Get file metadata first
    LIBMTP_file_t* file = LIBMTP_Get_Filemetadata(_device, objectId);
    if (!file) {
        DBG("GetFileContent: Could not get file metadata for ID %u", objectId);
        return ENOENT;
    }
    
    // Check if it's actually a file (not a directory)
    if (file->filetype == LIBMTP_FILETYPE_FOLDER) {
        DBG("GetFileContent: Object %u is a directory, not a file", objectId);
        LIBMTP_destroy_file_t(file);
        return EISDIR;
    }
    
    // Limit read size to file size or buffer size, whichever is smaller
    size_t readSize = bufferSize;
    if (file->filesize < bufferSize) {
        readSize = file->filesize;
    }
    
    // For reading file content to buffer, we need to use a different approach
    // Create a temporary file and read from it
    std::string tempFile = "/tmp/mtp_read_" + std::to_string(objectId);
    
    // Download file to temporary location
    int result = LIBMTP_Get_File_To_File(_device, objectId, tempFile.c_str(), nullptr, nullptr);
    
    if (result == 0) {
        // Read from temporary file
        FILE* fp = fopen(tempFile.c_str(), "rb");
        if (fp) {
            bytesRead = fread(buffer, 1, readSize, fp);
            fclose(fp);
            remove(tempFile.c_str());
            DBG("GetFileContent: Successfully read %zu bytes from file ID %u", bytesRead, objectId);
            return 0;
        } else {
            remove(tempFile.c_str());
            DBG("GetFileContent: Failed to open temporary file");
            return EIO;
        }
    } else {
        LIBMTP_destroy_file_t(file);
        DBG("GetFileContent: Failed to download file ID %u, error: %d", objectId, result);
        return EIO;
    }
}

int MTPDevice::GetMTPFileSize(uint32_t objectId, uint64_t& fileSize)
{
    if (!_connected || !_device) {
        DBG("GetFileSize: Device not connected");
        return EIO;
    }
    
    if (objectId == 0) {
        DBG("GetFileSize: Invalid object ID");
        return EINVAL;
    }
    
    LIBMTP_file_t* file = LIBMTP_Get_Filemetadata(_device, objectId);
    if (!file) {
        DBG("GetFileSize: Could not get file metadata for ID %u", objectId);
        return ENOENT;
    }
    
    fileSize = file->filesize;
    LIBMTP_destroy_file_t(file);
    
    DBG("GetFileSize: File ID %u size: %llu bytes", objectId, (unsigned long long)fileSize);
    return 0;
}

bool MTPDevice::IsFile(uint32_t objectId)
{
    if (!_connected || !_device || objectId == 0) {
        return false;
    }
    
    LIBMTP_file_t* file = LIBMTP_Get_Filemetadata(_device, objectId);
    if (!file) {
        return false;
    }
    
    bool isFile = (file->filetype != LIBMTP_FILETYPE_FOLDER);
    LIBMTP_destroy_file_t(file);
    
    return isFile;
}

std::string MTPDevice::GetFileName(uint32_t objectId)
{
    if (!_connected || !_device || objectId == 0) {
        return "";
    }
    
    LIBMTP_file_t* file = LIBMTP_Get_Filemetadata(_device, objectId);
    if (!file) {
        return "";
    }
    
    std::string fileName = file->filename ? std::string(file->filename) : "";
    LIBMTP_destroy_file_t(file);
    
    return fileName;
}

