#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <sys/stat.h>
#include "farplug-wide.h"
#include <libmtp.h>

class MTPDevice;
struct PluginPanelItem;

class MTPFileSystem {
private:
    std::shared_ptr<MTPDevice> _mtpDevice;
    LIBMTP_mtpdevice_t* _device;
    LIBMTP_devicestorage_t* _storage;
    std::string _currentPath;
    uint32_t _currentObjectId;
    std::string _lastError;
    
    // Shadow mechanism state
    std::string _currentObject;
    std::map<std::string, std::string> _nameToEncodedId;
    std::map<std::string, std::string> _encodedIdToName;
    
public:
    MTPFileSystem(std::shared_ptr<MTPDevice> mtpDevice);
    virtual ~MTPFileSystem();
    
    // Basic navigation operations
    std::vector<PluginPanelItem> ListDirectory(const std::string& path);
    bool ChangeDirectory(const std::string& path);
    bool ChangeDirectory(uint32_t objectId);
    
    // Utility functions
    std::string GetCurrentPath() const;
    std::string GetLastError() const;
    
    // ID encoding/decoding functions (simplified to S and O only)
    std::string EncodeObjectId(uint32_t objectId) const;
    std::string EncodeStorageId(uint32_t storageId) const;
    uint32_t DecodeStorageId(const std::string& encodedId) const;
    uint32_t DecodeObjectId(const std::string& encodedId) const;
    
    // Helper functions for hex conversion with leading zeros
    std::string IntToHexStr(uint32_t value, int width = 8) const;
    uint32_t HexStrToInt(const std::string& hexStr) const;
    
    // Helper function to find object by filename
    uint32_t FindObjectByFilename(const std::string& filename);
    uint32_t FindStorageForObject(uint32_t objectId) const;
    
    // MTP bulk enumeration for better performance
    struct MTPObjectProperties {
        uint32_t objectHandle;
        std::string filename;
        uint32_t filetype;
        uint64_t filesize;
        uint32_t parentId;
        uint32_t storageId;
        uint32_t modificationDate;
    };
    
    std::vector<MTPObjectProperties> GetBulkObjectProperties(uint32_t storageId, uint32_t parentHandle);
    PluginPanelItem CreateFileItemFromProperties(const MTPObjectProperties& prop);
    
    // Shadow mechanism methods
    std::string GetCurrentObject() const;
    void SetCurrentObject(const std::string& encodedId);
    std::string GetEncodedIdForName(const std::string& name) const;
    std::string GetNameForEncodedId(const std::string& encodedId) const;
    void SubstituteNameWithEncodedId(PluginPanelItem& item);
    
    // Path management helpers
    std::string GetStorageName() const;
    void UpdatePathDown(uint32_t objectId);
    void UpdatePathUp();
    std::string GetParentObject(const std::string& encodedId) const;
    std::string GetCurrentEncodedId() const;
    std::string GetStorageDisplayName(LIBMTP_devicestorage_t* storage);
    
private:
    // Helper methods
    PluginPanelItem CreateStorageItem(LIBMTP_devicestorage_t* storage);
    PluginPanelItem CreateFileItem(LIBMTP_file_t* file);
    // Removed duplicate declarations - using public ones
    FILETIME ConvertMTPTimeToFILETIME(uint32_t mtpTime);
};