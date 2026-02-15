#include "MTPFileSystem.h"
#include "MTPLog.h"
#include "MTPDevice.h"
#include "MTPPlugin.h"
#include <WideMB.h>
#include <cstring>
#include <cstdlib>
#include <IntStrConv.h>

// MTP Protocol constants
#define MTP_FORMAT_ASSOCIATION 0x3001
#define MTP_FORMAT_JPEG        0x3009
#define MTP_FORMAT_MP3         0x300B
#define MTP_FORMAT_AVI         0x300C
#define MTP_FORMAT_MP4         0x300D

MTPFileSystem::MTPFileSystem(std::shared_ptr<MTPDevice> mtpDevice)
    : _mtpDevice(mtpDevice)
    , _currentPath("/")
    , _currentObjectId(0)
    , _lastError("")
    , _currentObject("")
{
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
    auto device = _mtpDevice->GetDevice();
    
    if (!device) {
        _lastError = "MTP device not connected";
        return items;
    }
    
    try {
        if (path == "/" || path.empty()) {
            LIBMTP_devicestorage_t* storage = device->storage;
            while (storage) {
                items.push_back(CreateStorageItem(storage));
                storage = storage->next;
            }
        } else {
            if (MTPPlugin::IsEncodedId(path)) {
                uint32_t objectId;
                if (path[0] == 'S') {
                    objectId = DecodeStorageId(path);
                    if (objectId != 0) {
                        DBG("Getting root-level objects for storage ID: %u", objectId);
                        std::vector<MTPObjectProperties> props = GetBulkObjectProperties(objectId, LIBMTP_FILES_AND_FOLDERS_ROOT);
                        for (const auto& prop : props) {
                            DBG("=== Root Object ===");
                            DBG("Root ID: %u", prop.objectHandle);
                            DBG("Root Parent ID: %u", prop.parentId);
                            DBG("Root Storage ID: %u", prop.storageId);
                            DBG("Root Type: %u (0x%x)", prop.filetype, prop.filetype);
                            DBG("Root Filename: %s", prop.filename.c_str());
                            DBG("Root Size: %llu", (unsigned long long)prop.filesize);
                            items.push_back(CreateFileItemFromProperties(prop));
                        }
                        DBG("Found %zu root-level objects in storage %u", props.size(), objectId);
                    }
                } else if (path[0] == 'O') {
                    objectId = DecodeObjectId(path);
                    if (objectId != 0) {
                        uint32_t storageId = FindStorageForObject(objectId);
                        if (storageId != 0) {
                            DBG("Getting child objects for object ID: %u (storage: %u)", objectId, storageId);
                            std::vector<MTPObjectProperties> props = GetBulkObjectProperties(storageId, objectId);
                            for (const auto& prop : props) {
                                DBG("=== Child Object ===");
                                DBG("Child ID: %u", prop.objectHandle);
                                DBG("Child Parent ID: %u", prop.parentId);
                                DBG("Child Storage ID: %u", prop.storageId);
                                DBG("Child Type: %u (0x%x)", prop.filetype, prop.filetype);
                                DBG("Child Filename: %s", prop.filename.c_str());
                                DBG("Child Size: %llu", (unsigned long long)prop.filesize);
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
    
    auto device = _mtpDevice->GetDevice();
    auto storage_ptr = _mtpDevice->GetStorage();

    if (!device) {
        _lastError = "MTP device not connected";
        return false;
    }
    
    _nameToEncodedId.clear();
    _encodedIdToName.clear();
    
    try {
        if (path == "..") {
            DBG("Processing '..' navigation");
            if (_currentObjectId == 0) {
                _currentPath = "/";
                _currentObjectId = 0;
                // _storage was nullptr here effectively
                _currentObject = "";
                DBG("Navigated from storage root to device root");
                return true;
            }
            
            LIBMTP_file_t* currentObject = LIBMTP_Get_Filemetadata(device, _currentObjectId);
            if (!currentObject) {
                _lastError = "Cannot get current object metadata";
                DBG("ERROR: %s", _lastError.c_str());
                return false;
            }
            
            uint32_t parentId = currentObject->parent_id;
            LIBMTP_destroy_file_t(currentObject);
            
            if (parentId == 0) {
                _currentObjectId = 0;
                _currentPath = "/";
                _mtpDevice->SetCurrentDir(0, GetStorageName());
                DBG("Navigated to storage root: %s", _currentPath.c_str());
            } else {
                _currentObjectId = parentId;
                _currentObject = EncodeObjectId(parentId);
                UpdatePathUp();
                _mtpDevice->NavigateUp();
                DBG("Navigated to parent directory: ID=%u, Path=%s", parentId, _currentPath.c_str());
            }
            return true;
        }
        
        if (MTPPlugin::IsEncodedId(path) && path[0] == 'O') {
            uint32_t objectId = DecodeObjectId(path);
            if (objectId == 0) {
                _lastError = "Invalid object ID: " + path;
                DBG("ERROR: %s", _lastError.c_str());
                return false;
            }
            
            if (!storage_ptr) {
                _lastError = "No storage selected. Please select a storage first.";
                DBG("ERROR: %s", _lastError.c_str());
                return false;
            }
            
            DBG("ChangeDirectory: Testing if object %u is a directory (storage %u)", objectId, storage_ptr->id);
            
            LIBMTP_file_t* objectFile = LIBMTP_Get_Filemetadata(device, objectId);
            if (!objectFile) {
                _lastError = "Object not found: " + path;
                DBG("ERROR: %s", _lastError.c_str());
                return false;
            }
            
            if (objectFile->filetype != LIBMTP_FILETYPE_FOLDER) {
                _lastError = "Object is not a directory: " + path;
                DBG("ERROR: %s", _lastError.c_str());
                LIBMTP_destroy_file_t(objectFile);
                return false;
            }
            
            LIBMTP_destroy_file_t(objectFile);
            
            LIBMTP_file_t* files = LIBMTP_Get_Files_And_Folders(device, storage_ptr->id, objectId);
            if (files) {
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
            
            _currentObjectId = objectId;
            _currentObject = path;
            UpdatePathDown(objectId);
            
            DBG("Successfully changed to directory: %s (ID: %u)", path.c_str(), objectId);
            return true;
        }
        
        if (MTPPlugin::IsEncodedId(path) && path[0] == 'S') {
            uint32_t storageId = DecodeStorageId(path);
            if (storageId == 0) {
                _lastError = "Invalid storage ID: " + path;
                DBG("ERROR: %s", _lastError.c_str());
                return false;
            }
            
            LIBMTP_devicestorage_t* storage = device->storage;
            while (storage) {
                if (storage->id == storageId) {
                    _currentObjectId = 0;
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
        
        if (path == "/" || path.empty()) {
            _currentPath = "/";
            _currentObjectId = 0;
            _currentObject = "";
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
        auto storage = _mtpDevice->GetStorage();
        if (storage) {
            return EncodeStorageId(storage->id);
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

PluginPanelItem MTPFileSystem::CreatePanelItem(const std::string& name, const std::string& description, 
                                          uint64_t size, bool isDir, uint32_t modificationTime, 
                                          const std::string& userData)
{
    PluginPanelItem item = {{{0}}};
    
    // UserData
    if (!userData.empty()) {
        char* userDataPtr = (char*)malloc(userData.length() + 1);
        if (userDataPtr) {
            strcpy(userDataPtr, userData.c_str());
            item.UserData = (DWORD_PTR)userDataPtr;
        }
    }
    
    // Name
    std::wstring wideName = StrMB2Wide(name);
    item.FindData.lpwszFileName = (wchar_t*)calloc(wideName.length() + 1, sizeof(wchar_t));
    if (item.FindData.lpwszFileName) {
        wcscpy((wchar_t*)item.FindData.lpwszFileName, wideName.c_str());
    }
    
    // Attributes
    if (isDir) {
        item.FindData.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        item.FindData.dwUnixMode = S_IFDIR | 0755;
    } else {
        item.FindData.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        item.FindData.dwUnixMode = S_IFREG | 0644;
    }
    
    // Size
    item.FindData.nFileSize = item.FindData.nPhysicalSize = size;
    
    // Times
    item.FindData.ftCreationTime = {0};
    item.FindData.ftLastAccessTime = {0};
    item.FindData.ftLastWriteTime = ConvertMTPTimeToFILETIME(modificationTime);
    
    // Description
    if (!description.empty()) {
        std::wstring wideDesc = StrMB2Wide(description);
        item.Description = (wchar_t*)calloc(wideDesc.length() + 1, sizeof(wchar_t));
        if (item.Description) {
            wcscpy((wchar_t*)item.Description, wideDesc.c_str());
        }
    }
    
    return item;
}

PluginPanelItem MTPFileSystem::CreateStorageItem(LIBMTP_devicestorage_t* storage)
{
    if (!storage) return {{{0}}};
    
    std::string encodedId = EncodeStorageId(storage->id);
    std::string displayName = GetStorageDisplayName(storage);
    
    _nameToEncodedId[displayName] = encodedId;
    _encodedIdToName[encodedId] = displayName;
    
    return CreatePanelItem(displayName, encodedId, storage->MaxCapacity, true, 0, encodedId);
}

PluginPanelItem MTPFileSystem::CreateFileItem(LIBMTP_file_t* file)
{
    if (!file) return {{{0}}};
    
    std::string encodedId = EncodeObjectId(file->item_id);
    
    std::string displayName;
    if (file->filename) {
        displayName = std::string(file->filename);
    } else {
        displayName = (file->filetype == LIBMTP_FILETYPE_FOLDER) ? "Folder" : "File";
    }
    
    _nameToEncodedId[displayName] = encodedId;
    _encodedIdToName[encodedId] = displayName;
    
    bool isDir = (file->filetype == LIBMTP_FILETYPE_FOLDER);
    
    return CreatePanelItem(displayName, encodedId, file->filesize, isDir, file->modificationdate, encodedId);
}


std::string MTPFileSystem::EncodeObjectId(uint32_t objectId) const
{
    return "O" + IntToHexStr(objectId);
}

std::string MTPFileSystem::EncodeStorageId(uint32_t storageId) const
{
    return "S" + IntToHexStr(storageId);
}

uint32_t MTPFileSystem::DecodeStorageId(const std::string& encodedId) const
{
    if (encodedId.length() < 2 || encodedId[0] != 'S') {
        return 0;
    }
    return HexStrToInt(encodedId.substr(1));
}

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
    auto device = _mtpDevice->GetDevice();
    auto storage = _mtpDevice->GetStorage();
    if (!device) {
        return 0;
    }
    
    // Search in current directory
    if (_currentObjectId == 0 && storage) {
        // We're at storage root, search root-level objects
        LIBMTP_file_t* files = LIBMTP_Get_Files_And_Folders(device, storage->id, LIBMTP_FILES_AND_FOLDERS_ROOT);
        if (files) {
        LIBMTP_file_t* file = files;
        while (file) {
                if (file->item_id != LIBMTP_FILES_AND_FOLDERS_ROOT && file->filename && std::string(file->filename) == filename) {
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
    } else if (_currentObjectId != 0 && storage) {
        // We're in a directory, search its children
        LIBMTP_file_t* files = LIBMTP_Get_Files_And_Folders(device, storage->id, _currentObjectId);
        if (files) {
            LIBMTP_file_t* file = files;
            while (file) {
                if (file->item_id != _currentObjectId && file->filename && std::string(file->filename) == filename) {
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
    auto device = _mtpDevice->GetDevice();
    if (!device || objectId == 0) {
        return 0;
    }
    
    try {
        // Get the object metadata to find its storage ID
        LIBMTP_file_t* file = LIBMTP_Get_Filemetadata(device, objectId);
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

DirectoryInfo MTPFileSystem::GetDirectoryInfo(const std::string& encodedId)
{
    if (encodedId.empty() || encodedId[0] != 'O') return DirectoryInfo();
    uint32_t objectId = DecodeObjectId(encodedId);
    return _mtpDevice->GetDirectoryInfo(objectId);
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
    auto device = _mtpDevice->GetDevice();
    
    if (!device) {
        DBG("GetBulkObjectProperties: No device connected");
        return properties;
    }
    
    try {
        // Use enhanced MTP bulk enumeration if available
        // For now, fall back to LIBMTP_Get_Files_And_Folders but with better structure
        DBG("GetBulkObjectProperties: Getting objects for storage %u, parent %u", storageId, parentHandle);
        
        LIBMTP_file_t* files = LIBMTP_Get_Files_And_Folders(device, storageId, parentHandle);
        if (!files) {
            DBG("GetBulkObjectProperties: No objects found");
            return properties;
        }
        
        LIBMTP_file_t* file = files;
        while (file) {
            if (file->item_id != parentHandle) {
                MTPObjectProperties prop;
                prop.objectHandle = file->item_id;
                prop.filename = file->filename ? std::string(file->filename) : "";
                prop.filetype = file->filetype;
                prop.filesize = file->filesize;
                prop.parentId = file->parent_id;
                prop.storageId = file->storage_id;
                prop.modificationDate = file->modificationdate;
                
                properties.push_back(prop);
            } else {
                DBG("GetBulkObjectProperties: Skipping parent object %u returned as child", parentHandle);
            }
            
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
    // Store the encoded ID in UserData as a pointer to allocated string
    std::string encodedId = EncodeObjectId(prop.objectHandle);
    
    // Use real MTP filename for display
    std::string displayName = prop.filename.empty() ? 
        (prop.filetype == LIBMTP_FILETYPE_FOLDER ? "Folder" : "File") : 
        prop.filename;
    
    // Build shadow mechanism mappings
    _nameToEncodedId[displayName] = encodedId;
    _encodedIdToName[encodedId] = displayName;
    
    bool isDir = (prop.filetype == LIBMTP_FILETYPE_FOLDER);
    
    return CreatePanelItem(displayName, encodedId, prop.filesize, isDir, prop.modificationDate, encodedId);
}

std::string MTPFileSystem::GetParentObject(const std::string& encodedId) const
{
    auto device = _mtpDevice->GetDevice();
    auto storage = _mtpDevice->GetStorage();
    if (!device || encodedId.empty()) {
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
        LIBMTP_file_t* file = LIBMTP_Get_Filemetadata(device, objectId);
        if (!file) {
            return "";
        }
        
        uint32_t parentId = file->parent_id;
        LIBMTP_destroy_file_t(file);
        
        if (parentId == 0) {
            // Parent is storage root - return current storage
            if (storage) {
                return EncodeStorageId(storage->id);
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
    auto storage = _mtpDevice->GetStorage();
    if (!storage) {
        return "Unknown Storage";
    }
    
    if (storage->StorageDescription && strlen(storage->StorageDescription) > 0) {
        return std::string(storage->StorageDescription);
    }
    
    if (storage->VolumeIdentifier && strlen(storage->VolumeIdentifier) > 0) {
        return std::string(storage->VolumeIdentifier);
    }
    
    // Fallback to storage type-based name
    switch (storage->StorageType) {
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
    auto device = _mtpDevice->GetDevice();
    if (!device) return;

    // Get object name for path
    ScopedMTPFile file(LIBMTP_Get_Filemetadata(device, objectId));
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
    } else {
        DBG("UpdatePathDown: Could not get object name for ID %u", objectId);
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