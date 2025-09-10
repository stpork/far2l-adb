/**
 * @file MTPFileSystem.cpp
 * @brief Simplified MTP File System Implementation for Navigation Only
 * 
 * This file implements a simplified MTP file system that provides only
 * the essential functionality for device enumeration, storage listing,
 * and directory navigation using encoded IDs.
 * 
 * @author MTP Plugin Team
 * @version 1.0
 */

#include "MTPFileSystem.h"
#include "MTPLog.h"
#include "MTPDevice.h"
#include "MTPPlugin.h"

// MTP Protocol constants (not exposed by libmtp)
#define MTP_FORMAT_ASSOCIATION 0x3001  // Association object (folder)
#define MTP_FORMAT_JPEG        0x3009  // JPEG image
#define MTP_FORMAT_MP3         0x300B  // MP3 audio
#define MTP_FORMAT_AVI         0x300C  // AVI video
#define MTP_FORMAT_MP4         0x300D  // MP4 video
#include <WideMB.h>
#include <cstring>
#include <cstdlib>
#include <IntStrConv.h>

MTPFileSystem::MTPFileSystem(std::shared_ptr<MTPDevice> mtpDevice)
    : _mtpDevice(mtpDevice)
    , _device(nullptr)
    , _storage(nullptr)
    , _currentPath("/")
    , _currentObjectId(0)
    , _lastError("")
    , _currentObject("")
{
    if (_mtpDevice) {
        _device = _mtpDevice->GetDevice();
        _storage = _mtpDevice->GetStorage();
    }
    
    DBG("MTPFileSystem initialized");
}

MTPFileSystem::~MTPFileSystem()
{
    DBG("MTPFileSystem destroyed");
}

std::vector<PluginPanelItem> MTPFileSystem::ListDirectory(const std::string& path)
{
    DBG("Listing directory: %s", path.c_str());
    
    std::vector<PluginPanelItem> items;
    
    if (!_device) {
        _lastError = "MTP device not connected";
        return items;
    }
    
    try {
        if (path == "/" || path.empty()) {
            // List storages (root level)
            LIBMTP_devicestorage_t* storage = _device->storage;
            while (storage) {
                items.push_back(CreateStorageItem(storage));
                storage = storage->next;
            }
        } else {
            // Check if path is an encoded ID
            if (MTPPlugin::IsEncodedId(path)) {
                uint32_t objectId;
                
                if (path[0] == 'S') {
                    // Decode storage ID and get root-level objects
                    objectId = DecodeStorageId(path);
                    if (objectId != 0) {
                        // For storage, get root-level objects (parent = LIBMTP_FILES_AND_FOLDERS_ROOT)
                        DBG("Getting root-level objects for storage ID: %u", objectId);
                        
                        // Use bulk enumeration for better performance
                        std::vector<MTPObjectProperties> props = GetBulkObjectProperties(objectId, LIBMTP_FILES_AND_FOLDERS_ROOT);
                        
                        for (const auto& prop : props) {
                            // Log root-level object details
                            DBG("=== Root Object ===");
                            DBG("Root ID: %u", prop.objectHandle);
                            DBG("Root Parent ID: %u", prop.parentId);
                            DBG("Root Storage ID: %u", prop.storageId);
                            DBG("Root Type: %u (0x%x)", prop.filetype, prop.filetype);
                            DBG("Root Filename: %s", prop.filename.c_str());
                            DBG("Root Size: %llu", (unsigned long long)prop.filesize);
                            
                            // Create PluginPanelItem from properties
                            items.push_back(CreateFileItemFromProperties(prop));
                        }
                        
                        DBG("Found %zu root-level objects in storage %u", props.size(), objectId);
                    }
                } else if (path[0] == 'O') {
                    // Decode object ID (directory or file)
                    objectId = DecodeObjectId(path);
                    
                    if (objectId != 0) {
                        // For directory/file/object, get children of this specific object
                        // First, find which storage this object belongs to
                        uint32_t storageId = FindStorageForObject(objectId);
                        if (storageId != 0) {
                            DBG("Getting child objects for object ID: %u (storage: %u)", objectId, storageId);
                            
                            // Use bulk enumeration for better performance
                            std::vector<MTPObjectProperties> props = GetBulkObjectProperties(storageId, objectId);
                            
                            for (const auto& prop : props) {
                                // Log child object details
                                DBG("=== Child Object ===");
                                DBG("Child ID: %u", prop.objectHandle);
                                DBG("Child Parent ID: %u", prop.parentId);
                                DBG("Child Storage ID: %u", prop.storageId);
                                DBG("Child Type: %u (0x%x)", prop.filetype, prop.filetype);
                                DBG("Child Filename: %s", prop.filename.c_str());
                                DBG("Child Size: %llu", (unsigned long long)prop.filesize);
                                
                                // Create PluginPanelItem from properties
                                items.push_back(CreateFileItemFromProperties(prop));
                            }
                            
                            DBG("Found %zu child objects for parent ID %u", props.size(), objectId);
                        } else {
                            DBG("No child objects found for parent ID %u", objectId);
            }
        } else {
                        DBG("Could not find storage for object %u", objectId);
                    }
                }
            }
        }
        
        DBG("Found %zu items in directory", items.size());
        
        // If we found no items, this might be an empty directory
        if (items.empty()) {
            DBG("ListDirectory: Empty directory detected - this is normal for empty directories");
        }
        
    } catch (const std::exception& e) {
        _lastError = "Exception in ListDirectory: " + std::string(e.what());
        DBG("ERROR: %s", _lastError.c_str());
    }
    
    return items;
}

bool MTPFileSystem::ChangeDirectory(const std::string& path)
{
    DBG("=== ChangeDirectory called ===");
    DBG("Path argument: '%s' (length: %zu)", path.c_str(), path.length());
    
    if (!_device) {
        _lastError = "MTP device not connected";
        return false;
    }
    
    // Clear shadow mechanism mappings when changing directories
    _nameToEncodedId.clear();
    _encodedIdToName.clear();
    
    try {
        // Handle ".." navigation
        if (path == "..") {
            DBG("Processing '..' navigation");
            
            if (_currentObjectId == 0) {
                // We're at storage root, go back to device root
                _currentPath = "/";
                _currentObjectId = 0;
                _storage = nullptr;
                _currentObject = "";
                DBG("Navigated from storage root to device root");
                return true;
            }
            
            // Get parent object ID from current object
            LIBMTP_file_t* currentObject = LIBMTP_Get_Filemetadata(_device, _currentObjectId);
            if (!currentObject) {
                _lastError = "Cannot get current object metadata";
                DBG("ERROR: %s", _lastError.c_str());
        return false;
            }
            
            uint32_t parentId = currentObject->parent_id;
            LIBMTP_destroy_file_t(currentObject);
            
            if (parentId == 0) {
                // Parent is storage root
                _currentObjectId = 0;
                _currentPath = "/";
                _mtpDevice->SetCurrentDir(0, GetStorageName());
                DBG("Navigated to storage root: %s", _currentPath.c_str());
            } else {
                // Navigate to parent directory
                _currentObjectId = parentId;
                _currentObject = EncodeObjectId(parentId);
                // Update path by removing last directory
                UpdatePathUp();
                // Update MTPDevice's centralized path
                _mtpDevice->NavigateUp();
                DBG("Navigated to parent directory: ID=%u, Path=%s", parentId, _currentPath.c_str());
            }
            return true;
        }
        
        // Handle encoded object IDs (Oxxxxxx)
        if (MTPPlugin::IsEncodedId(path) && path[0] == 'O') {
            uint32_t objectId = DecodeObjectId(path);
            if (objectId == 0) {
                _lastError = "Invalid object ID: " + path;
                DBG("ERROR: %s", _lastError.c_str());
            return false;
        }
    
            // Check if object exists and has children (is a directory)
            if (!_storage) {
                _lastError = "No storage selected. Please select a storage first.";
                DBG("ERROR: %s", _lastError.c_str());
        return false;
        }
            
            DBG("ChangeDirectory: Testing if object %u is a directory (storage %u)", objectId, _storage->id);
            
            // First check if the object exists by getting its metadata
            LIBMTP_file_t* objectFile = LIBMTP_Get_Filemetadata(_device, objectId);
            if (!objectFile) {
                _lastError = "Object not found: " + path;
                DBG("ERROR: %s", _lastError.c_str());
        return false;
    }
    
            // Check if it's a directory (association object)
            // libmtp converts MTP protocol values to enum values
            if (objectFile->filetype != LIBMTP_FILETYPE_FOLDER) {
                _lastError = "Object is not a directory: " + path;
                DBG("ERROR: %s", _lastError.c_str());
                LIBMTP_destroy_file_t(objectFile);
                return false;
            }
            
            // It's a directory - clean up the metadata
            LIBMTP_destroy_file_t(objectFile);
            
            // Test if it has children (but don't fail if it doesn't)
            LIBMTP_file_t* files = LIBMTP_Get_Files_And_Folders(_device, _storage->id, objectId);
            if (files) {
                // Has children - clean up the test call
                LIBMTP_file_t* file = files;
                while (file) {
                    LIBMTP_file_t* oldfile = file;
                    file = file->next;
                    LIBMTP_destroy_file_t(oldfile);
                }
                DBG("ChangeDirectory: Object %u is a directory with children", objectId);
            } else {
                DBG("ChangeDirectory: Object %u is an empty directory", objectId);
            }
            
            // Object exists and has children - navigate to it
            _currentObjectId = objectId;
            _currentObject = path;
            UpdatePathDown(objectId);
            
            DBG("Successfully changed to directory: %s (ID: %u)", path.c_str(), objectId);
            return true;
        }
        
        // Handle storage IDs (Sxxxxxx)
        if (MTPPlugin::IsEncodedId(path) && path[0] == 'S') {
            uint32_t storageId = DecodeStorageId(path);
            if (storageId == 0) {
                _lastError = "Invalid storage ID: " + path;
                DBG("ERROR: %s", _lastError.c_str());
        return false;
    }
    
            // Find and set the current storage
            LIBMTP_devicestorage_t* storage = _device->storage;
            while (storage) {
                if (storage->id == storageId) {
                    _storage = storage;
                    _currentObjectId = 0;  // Root level in storage
                    _currentObject = "";
                    _currentPath = "/";
                    _mtpDevice->SetCurrentStorage(storageId, GetStorageName());
                    DBG("Successfully changed to storage: %s (ID: %u)", path.c_str(), storageId);
                    return true;
                }
                storage = storage->next;
            }
            _lastError = "Storage not found: " + path;
            DBG("ERROR: %s", _lastError.c_str());
            return false;
        }
        
        // Handle root directory
        if (path == "/" || path.empty()) {
            _currentPath = "/";
            _currentObjectId = 0;
            _currentObject = "";
            _storage = nullptr;
            _mtpDevice->NavigateToRoot();
            DBG("Changed to root directory");
            return true;
        }
        
        _lastError = "Invalid directory path: " + path;
        DBG("ERROR: %s", _lastError.c_str());
        return false;
        
    } catch (const std::exception& e) {
        _lastError = "Exception in ChangeDirectory: " + std::string(e.what());
        DBG("%s", _lastError.c_str());
        return false;
    }
}

bool MTPFileSystem::ChangeDirectory(uint32_t objectId)
{
    std::string encodedId = EncodeObjectId(objectId);
    return ChangeDirectory(encodedId);
}

std::string MTPFileSystem::GetCurrentPath() const
{
    return _currentPath;
}

std::string MTPFileSystem::GetCurrentEncodedId() const
{
    if (_currentObjectId == 0) {
        // At storage root, return the storage encoded ID
        if (_storage) {
            return EncodeStorageId(_storage->id);
        } else {
            return ""; // At device root
        }
    } else {
        // In a directory, return the object encoded ID
        return EncodeObjectId(_currentObjectId);
    }
}

std::string MTPFileSystem::GetLastError() const
{
    return _lastError;
}

PluginPanelItem MTPFileSystem::CreateStorageItem(LIBMTP_devicestorage_t* storage)
{
    PluginPanelItem item = {{{0}}};
    
    if (!storage) return item;
    
    // Create encoded storage ID
    std::string encodedId = EncodeStorageId(storage->id);
    
    // Store the encoded ID in UserData as a pointer to allocated string
    char* encodedIdPtr = (char*)malloc(encodedId.length() + 1);
    if (encodedIdPtr) {
        strcpy(encodedIdPtr, encodedId.c_str());
        item.UserData = (DWORD_PTR)encodedIdPtr;
    } else {
        item.UserData = 0;
    }
    
    // Determine storage display name using StorageInfo fields
    std::string displayName = GetStorageDisplayName(storage);
    
    // Build shadow mechanism mappings (no decoration needed)
    _nameToEncodedId[displayName] = encodedId;
    _encodedIdToName[encodedId] = displayName;
    
    // Set file name using clean display name
    std::wstring wideDisplayName = StrMB2Wide(displayName);
    item.FindData.lpwszFileName = (wchar_t*)calloc(wideDisplayName.length() + 1, sizeof(wchar_t));
    if (item.FindData.lpwszFileName) {
        // Copy the string content
        const wchar_t* src = wideDisplayName.c_str();
        wchar_t* dst = const_cast<wchar_t*>(item.FindData.lpwszFileName);
        while (*src) {
            *dst++ = *src++;
        }
        *dst = L'\0';
    }
    
    // Set as directory
    item.FindData.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    item.FindData.dwUnixMode = S_IFDIR | 0755;
    
    // Set size
    item.FindData.nFileSize = item.FindData.nPhysicalSize = storage->MaxCapacity;
    
    // Set description to encoded storage ID
    std::wstring wideEncodedId = StrMB2Wide(encodedId);
    item.Description = (wchar_t*)calloc(wideEncodedId.length() + 1, sizeof(wchar_t));
    if (item.Description) {
        // Copy the string content
        const wchar_t* src = wideEncodedId.c_str();
        wchar_t* dst = const_cast<wchar_t*>(item.Description);
        while (*src) {
            *dst++ = *src++;
        }
        *dst = L'\0';
    }
    
    return item;
}

PluginPanelItem MTPFileSystem::CreateFileItem(LIBMTP_file_t* file)
{
    PluginPanelItem item = {{{0}}};
    
    if (!file) return item;
    
    // Store the encoded ID in UserData as a pointer to allocated string
    // Use O encoding for all objects (directories and files)
    std::string encodedId = EncodeObjectId(file->item_id);
    
    // Allocate memory for the encoded ID string and store pointer in UserData
    char* encodedIdPtr = (char*)malloc(encodedId.length() + 1);
    if (encodedIdPtr) {
        strcpy(encodedIdPtr, encodedId.c_str());
        item.UserData = (DWORD_PTR)encodedIdPtr;
        } else {
        item.UserData = 0;
    }
    
    // Use real MTP filename for display
    std::string displayName;
    if (file->filename) {
        displayName = std::string(file->filename);
    } else {
        if (file->filetype == LIBMTP_FILETYPE_FOLDER) {
            displayName = "Folder";
        } else {
            displayName = "File";
        }
    }
    
    // Build shadow mechanism mappings (no decoration needed)
    _nameToEncodedId[displayName] = encodedId;
    _encodedIdToName[encodedId] = displayName;
    
    // Set file name using clean display name
    std::wstring wideDisplayName = StrMB2Wide(displayName);
    item.FindData.lpwszFileName = (wchar_t*)calloc(wideDisplayName.length() + 1, sizeof(wchar_t));
    if (item.FindData.lpwszFileName) {
        // Copy the string content
        const wchar_t* src = wideDisplayName.c_str();
        wchar_t* dst = const_cast<wchar_t*>(item.FindData.lpwszFileName);
        while (*src) {
            *dst++ = *src++;
        }
        *dst = L'\0';
    }
    
    // Set file attributes based on file type
    if (file->filetype == LIBMTP_FILETYPE_FOLDER) {
        item.FindData.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        item.FindData.dwUnixMode = S_IFDIR | 0755;
        } else {
        item.FindData.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        item.FindData.dwUnixMode = S_IFREG | 0644;
    }
    
    // Set file size
    item.FindData.nFileSize = item.FindData.nPhysicalSize = file->filesize;
    
    // Set timestamps from MTP data
    item.FindData.ftCreationTime = {0}; // MTP doesn't provide creation time
    item.FindData.ftLastAccessTime = {0}; // MTP doesn't provide last access time
    item.FindData.ftLastWriteTime = ConvertMTPTimeToFILETIME(file->modificationdate);
    
    // Set description to encoded object ID
    std::wstring wideEncodedId = StrMB2Wide(encodedId);
    item.Description = (wchar_t*)calloc(wideEncodedId.length() + 1, sizeof(wchar_t));
    if (item.Description) {
        // Copy the string content
        const wchar_t* src = wideEncodedId.c_str();
        wchar_t* dst = const_cast<wchar_t*>(item.Description);
        while (*src) {
            *dst++ = *src++;
        }
        *dst = L'\0';
    }
    
    // UserData already set above with encoded ID string pointer - DO NOT OVERWRITE!
    
    return item;
}

std::string MTPFileSystem::EncodeObjectId(uint32_t objectId) const
{
    return "O" + IntToHexStr(objectId);
}

std::string MTPFileSystem::EncodeStorageId(uint32_t storageId) const
{
    return "S" + IntToHexStr(storageId);
}

// Removed EncodeDirectoryId and EncodeFileId - using EncodeObjectId for all objects

uint32_t MTPFileSystem::DecodeStorageId(const std::string& encodedId) const
{
    if (encodedId.length() < 2 || encodedId[0] != 'S') {
        return 0;
    }
    return HexStrToInt(encodedId.substr(1));
}

// Removed DecodeDirectoryId and DecodeFileId - using DecodeObjectId for all objects

uint32_t MTPFileSystem::DecodeObjectId(const std::string& encodedId) const
{
    if (encodedId.length() < 2 || encodedId[0] != 'O') {
        return 0;
    }
    return HexStrToInt(encodedId.substr(1));
}

std::string MTPFileSystem::IntToHexStr(uint32_t value, int width) const
{
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%0*x", width, value);
    return std::string(buffer);
}

uint32_t MTPFileSystem::HexStrToInt(const std::string& hexStr) const
{
    try {
        return std::stoul(hexStr, nullptr, 16);
    } catch (const std::exception&) {
        return 0;
    }
}

FILETIME MTPFileSystem::ConvertMTPTimeToFILETIME(uint32_t mtpTime)
{
    FILETIME ft = {0};
    
    if (mtpTime == 0) {
        return ft;
    }
    
    // MTP modificationdate is a Unix timestamp (seconds since epoch)
    // Convert to FILETIME (100-nanosecond intervals since 1601-01-01)
    
    // Unix epoch (1970-01-01) to Windows epoch (1601-01-01) difference
    const uint64_t EPOCH_DIFFERENCE = 11644473600ULL; // seconds
    
    // Convert to 100-nanosecond intervals
    uint64_t fileTime = ((uint64_t)mtpTime + EPOCH_DIFFERENCE) * 10000000ULL;
    
    ft.dwLowDateTime = (DWORD)(fileTime & 0xFFFFFFFF);
    ft.dwHighDateTime = (DWORD)(fileTime >> 32);
    
    return ft;
}

uint32_t MTPFileSystem::FindObjectByFilename(const std::string& filename)
{
    if (!_device) {
        return 0;
    }
    
    // Search in current directory
    if (_currentObjectId == 0 && _storage) {
        // We're at storage root, search root-level objects
        LIBMTP_file_t* files = LIBMTP_Get_Files_And_Folders(_device, _storage->id, LIBMTP_FILES_AND_FOLDERS_ROOT);
        if (files) {
        LIBMTP_file_t* file = files;
        while (file) {
                if (file->filename && std::string(file->filename) == filename) {
                    uint32_t objectId = file->item_id;
                // Clean up
                    LIBMTP_file_t* temp = files;
                    while (temp) {
                        LIBMTP_file_t* oldfile = temp;
                        temp = temp->next;
                        LIBMTP_destroy_file_t(oldfile);
                    }
                    return objectId;
                }
                file = file->next;
            }
            // Clean up
            LIBMTP_file_t* temp = files;
            while (temp) {
                LIBMTP_file_t* oldfile = temp;
                temp = temp->next;
                LIBMTP_destroy_file_t(oldfile);
            }
        }
    } else if (_currentObjectId != 0 && _storage) {
        // We're in a directory, search its children
        LIBMTP_file_t* files = LIBMTP_Get_Files_And_Folders(_device, _storage->id, _currentObjectId);
        if (files) {
            LIBMTP_file_t* file = files;
            while (file) {
                if (file->filename && std::string(file->filename) == filename) {
                    uint32_t objectId = file->item_id;
                    // Clean up
                    LIBMTP_file_t* temp = files;
                    while (temp) {
                        LIBMTP_file_t* oldfile = temp;
                        temp = temp->next;
                        LIBMTP_destroy_file_t(oldfile);
                    }
                    return objectId;
                }
            file = file->next;
            }
            // Clean up
            LIBMTP_file_t* temp = files;
            while (temp) {
                LIBMTP_file_t* oldfile = temp;
                temp = temp->next;
            LIBMTP_destroy_file_t(oldfile);
        }
        }
    }
    
    return 0;
}

uint32_t MTPFileSystem::FindStorageForObject(uint32_t objectId) const
{
    if (!_device || objectId == 0) {
        return 0;
    }
    
    try {
        // Get the object metadata to find its storage ID
        LIBMTP_file_t* file = LIBMTP_Get_Filemetadata(_device, objectId);
        if (!file) {
            return 0;
        }
        
        uint32_t storageId = file->storage_id;
        LIBMTP_destroy_file_t(file);
        
        return storageId;
        
    } catch (const std::exception& e) {
        DBG("FindStorageForObject error: %s", e.what());
        return 0;
    }
}

// Shadow mechanism methods
std::string MTPFileSystem::GetCurrentObject() const
{
    return _currentObject;
}

void MTPFileSystem::SetCurrentObject(const std::string& encodedId)
{
    _currentObject = encodedId;
    DBG("SetCurrentObject: %s", encodedId.c_str());
}

std::string MTPFileSystem::GetEncodedIdForName(const std::string& name) const
{
    auto it = _nameToEncodedId.find(name);
    if (it != _nameToEncodedId.end()) {
        return it->second;
    }
    return "";
}

std::string MTPFileSystem::GetNameForEncodedId(const std::string& encodedId) const
{
    auto it = _encodedIdToName.find(encodedId);
    if (it != _encodedIdToName.end()) {
        return it->second;
    }
    return "";
}

void MTPFileSystem::SubstituteNameWithEncodedId(PluginPanelItem& item)
{
    if (!item.FindData.lpwszFileName) return;
    
    // Convert current filename to string
    std::wstring wideName(item.FindData.lpwszFileName);
    std::string name = StrWide2MB(wideName);
    
    // Look up encoded ID for this name
    std::string encodedId = GetEncodedIdForName(name);
    if (!encodedId.empty()) {
        // Substitute the filename with encoded ID
        std::wstring wideEncodedId = StrMB2Wide(encodedId);
        
        // Free old filename and allocate new one
        free((void*)item.FindData.lpwszFileName);
        item.FindData.lpwszFileName = (wchar_t*)calloc(wideEncodedId.length() + 1, sizeof(wchar_t));
        if (item.FindData.lpwszFileName) {
            const wchar_t* src = wideEncodedId.c_str();
            wchar_t* dst = const_cast<wchar_t*>(item.FindData.lpwszFileName);
            while (*src) {
                *dst++ = *src++;
            }
            *dst = L'\0';
        }
        
        DBG("SubstituteNameWithEncodedId: %s -> %s", name.c_str(), encodedId.c_str());
    }
}

std::string MTPFileSystem::GetStorageDisplayName(LIBMTP_devicestorage_t* storage)
{
    if (!storage) {
        return "Unknown Storage";
    }
    
    // Priority 1: Use StorageDescription if available and not empty
    if (storage->StorageDescription && strlen(storage->StorageDescription) > 0) {
        DBG("Using StorageDescription: '%s'", storage->StorageDescription);
        return std::string(storage->StorageDescription);
    }
    
    // Priority 2: Use VolumeIdentifier if available and not empty
    if (storage->VolumeIdentifier && strlen(storage->VolumeIdentifier) > 0) {
        DBG("Using VolumeIdentifier: '%s'", storage->VolumeIdentifier);
        return std::string(storage->VolumeIdentifier);
    }
    
    // Priority 3: Make assumption based on StorageType
    // MTP StorageType values (from MTP specification):
    // 0x0001 = Fixed RAM
    // 0x0002 = Removable RAM  
    // 0x0003 = Fixed ROM
    // 0x0004 = Removable ROM
    // 0x0005 = Fixed RAM (alternative)
    // 0x0006 = Removable RAM (alternative)
    
    std::string typeBasedName;
    switch (storage->StorageType) {
        case 0x0001: // Fixed RAM
        case 0x0005: // Fixed RAM (alternative)
            typeBasedName = "Phone Memory";
            break;
        case 0x0002: // Removable RAM
        case 0x0006: // Removable RAM (alternative)
            typeBasedName = "External Storage";
            break;
        case 0x0003: // Fixed ROM
            typeBasedName = "Internal ROM";
            break;
        case 0x0004: // Removable ROM
            typeBasedName = "External ROM";
            break;
        default:
            typeBasedName = "Storage";
            break;
    }
    
    DBG("Using StorageType-based name: '%s' (StorageType=0x%04X)", typeBasedName.c_str(), storage->StorageType);
    
    // Add capacity info if available
    if (storage->MaxCapacity > 0) {
        uint64_t capacityGB = storage->MaxCapacity / (1024 * 1024 * 1024);
        if (capacityGB > 0) {
            typeBasedName += " (" + std::to_string(capacityGB) + "GB)";
        } else {
            uint64_t capacityMB = storage->MaxCapacity / (1024 * 1024);
            if (capacityMB > 0) {
                typeBasedName += " (" + std::to_string(capacityMB) + "MB)";
            }
        }
    }
    
    return typeBasedName;
}

std::vector<MTPFileSystem::MTPObjectProperties> MTPFileSystem::GetBulkObjectProperties(uint32_t storageId, uint32_t parentHandle)
{
    std::vector<MTPObjectProperties> properties;
    
    if (!_device) {
        DBG("GetBulkObjectProperties: No device connected");
        return properties;
    }
    
    try {
        // Use enhanced MTP bulk enumeration if available
        // For now, fall back to LIBMTP_Get_Files_And_Folders but with better structure
        DBG("GetBulkObjectProperties: Getting objects for storage %u, parent %u", storageId, parentHandle);
        
        LIBMTP_file_t* files = LIBMTP_Get_Files_And_Folders(_device, storageId, parentHandle);
        if (!files) {
            DBG("GetBulkObjectProperties: No objects found");
            return properties;
        }
        
        LIBMTP_file_t* file = files;
        while (file) {
            MTPObjectProperties prop;
            prop.objectHandle = file->item_id;
            prop.filename = file->filename ? std::string(file->filename) : "";
            prop.filetype = file->filetype;
            prop.filesize = file->filesize;
            prop.parentId = file->parent_id;
            prop.storageId = file->storage_id;
            prop.modificationDate = file->modificationdate;
            
            properties.push_back(prop);
            
            LIBMTP_file_t* oldfile = file;
            file = file->next;
            LIBMTP_destroy_file_t(oldfile);
        }
        
        DBG("GetBulkObjectProperties: Retrieved %zu object properties", properties.size());
        
    } catch (const std::exception& e) {
        DBG("GetBulkObjectProperties error: %s", e.what());
    }
    
    return properties;
}

PluginPanelItem MTPFileSystem::CreateFileItemFromProperties(const MTPObjectProperties& prop)
{
    PluginPanelItem item = {{{0}}};
    
    // Store the encoded ID in UserData as a pointer to allocated string
    // Use O encoding for all objects (directories and files)
    std::string encodedId = EncodeObjectId(prop.objectHandle);
    
    // Allocate memory for the encoded ID string and store pointer in UserData
    char* encodedIdPtr = (char*)malloc(encodedId.length() + 1);
    if (encodedIdPtr) {
        strcpy(encodedIdPtr, encodedId.c_str());
        item.UserData = (DWORD_PTR)encodedIdPtr;
    } else {
        item.UserData = 0;
    }
    
    // Use real MTP filename for display
    std::string displayName = prop.filename.empty() ? 
        (prop.filetype == LIBMTP_FILETYPE_FOLDER ? "Folder" : "File") : 
        prop.filename;
    
    // Build shadow mechanism mappings (no decoration needed)
    _nameToEncodedId[displayName] = encodedId;
    _encodedIdToName[encodedId] = displayName;
    
    // Set file name using clean display name
    std::wstring wideDisplayName = StrMB2Wide(displayName);
    item.FindData.lpwszFileName = (wchar_t*)calloc(wideDisplayName.length() + 1, sizeof(wchar_t));
    if (item.FindData.lpwszFileName) {
        // Copy the string content
        const wchar_t* src = wideDisplayName.c_str();
        wchar_t* dst = const_cast<wchar_t*>(item.FindData.lpwszFileName);
        while (*src) {
            *dst++ = *src++;
        }
        *dst = L'\0';
    }
    
    // Set file attributes based on file type
    DBG("CreateFileItemFromProperties: object=%u, filename='%s', filetype=0x%x", 
        prop.objectHandle, prop.filename.c_str(), prop.filetype);
    
    if (prop.filetype == LIBMTP_FILETYPE_FOLDER) {
        item.FindData.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        item.FindData.dwUnixMode = S_IFDIR | 0755;
        DBG("Set as DIRECTORY: %s", prop.filename.c_str());
    } else {
        item.FindData.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        item.FindData.dwUnixMode = S_IFREG | 0644;
        DBG("Set as FILE: %s", prop.filename.c_str());
    }
    
    // Set file size
    item.FindData.nFileSize = item.FindData.nPhysicalSize = prop.filesize;
    
    // Set timestamps from MTP data
    item.FindData.ftCreationTime = {0}; // MTP doesn't provide creation time
    item.FindData.ftLastAccessTime = {0}; // MTP doesn't provide last access time
    item.FindData.ftLastWriteTime = ConvertMTPTimeToFILETIME(prop.modificationDate);
    
    // Set description to encoded object ID
    std::wstring wideEncodedId = StrMB2Wide(encodedId);
    item.Description = (wchar_t*)calloc(wideEncodedId.length() + 1, sizeof(wchar_t));
    if (item.Description) {
        // Copy the string content
        const wchar_t* src = wideEncodedId.c_str();
        wchar_t* dst = const_cast<wchar_t*>(item.Description);
        while (*src) {
            *dst++ = *src++;
        }
        *dst = L'\0';
    }
    
    return item;
}

std::string MTPFileSystem::GetParentObject(const std::string& encodedId) const
{
    if (!_device || encodedId.empty()) {
        return "";
    }
    
    try {
        uint32_t objectId = 0;
        
        // Decode the object ID based on type
        if (encodedId[0] == 'S') {
            objectId = DecodeStorageId(encodedId);
        } else if (encodedId[0] == 'O') {
            objectId = DecodeObjectId(encodedId);
        } else {
            return "";
        }
        
        if (objectId == 0) {
            return "";
        }
        
        // Get the object metadata to find parent
        LIBMTP_file_t* file = LIBMTP_Get_Filemetadata(_device, objectId);
        if (!file) {
            return "";
        }
        
        uint32_t parentId = file->parent_id;
        LIBMTP_destroy_file_t(file);
        
        if (parentId == 0) {
            // Parent is storage root - return current storage
            if (_storage) {
                return EncodeStorageId(_storage->id);
            }
            return "";
        }
        
        // Return encoded parent ID
        return EncodeObjectId(parentId);
        
    } catch (const std::exception& e) {
        DBG("GetParentObject error: %s", e.what());
        return "";
    }
}

std::string MTPFileSystem::GetStorageName() const
{
    if (!_storage) {
        return "Unknown Storage";
    }
    
    if (_storage->StorageDescription && strlen(_storage->StorageDescription) > 0) {
        return std::string(_storage->StorageDescription);
    }
    
    if (_storage->VolumeIdentifier && strlen(_storage->VolumeIdentifier) > 0) {
        return std::string(_storage->VolumeIdentifier);
    }
    
    // Fallback to storage type-based name
    switch (_storage->StorageType) {
        case 0x0001: // Fixed RAM
        case 0x0005: // Fixed RAM (alternative)
            return "Phone Memory";
        case 0x0002: // Removable RAM
        case 0x0006: // Removable RAM (alternative)
            return "External Storage";
        case 0x0003: // Fixed ROM
            return "Internal ROM";
        case 0x0004: // Removable ROM
            return "External ROM";
        default:
            return "Storage";
    }
}

void MTPFileSystem::UpdatePathDown(uint32_t objectId)
{
    // Get object name for path
    LIBMTP_file_t* file = LIBMTP_Get_Filemetadata(_device, objectId);
    if (file && file->filename) {
        std::string dirName = std::string(file->filename);
        if (!_currentPath.empty() && _currentPath.back() == '/') {
            _currentPath += dirName + "/";
        } else {
            _currentPath = "/" + dirName + "/";
        }
        
        // Update MTPDevice's centralized path
        _mtpDevice->SetCurrentDir(objectId, dirName);
        
        DBG("UpdatePathDown: Added '%s' to path: %s", dirName.c_str(), _currentPath.c_str());
        LIBMTP_destroy_file_t(file);
    } else {
        DBG("UpdatePathDown: Could not get object name for ID %u", objectId);
        if (file) LIBMTP_destroy_file_t(file);
    }
}

void MTPFileSystem::UpdatePathUp()
{
    // Remove last directory from path
    if (_currentPath.length() > 1) {
        size_t lastSlash = _currentPath.find_last_of('/', _currentPath.length() - 2);
        if (lastSlash != std::string::npos) {
            _currentPath = _currentPath.substr(0, lastSlash + 1);
            DBG("UpdatePathUp: Path updated to: %s", _currentPath.c_str());
        }
    }
}