#pragma once

#include <string>
#include <time.h>
#include <sys/stat.h>
#include <libmtp.h>

struct PluginPanelItem;

class MTPDevice {
private:
    std::string _device_id;
    std::string _current_path;
    LIBMTP_mtpdevice_t* _device;
    LIBMTP_devicestorage_t* _storage;
    bool _connected;
    
    // Device properties
    std::string _friendlyName;
    std::string _manufacturer;
    std::string _model;
    std::string _serialNumber;
    
    // Current state
    uint32_t _currentStorageId;
    uint32_t _currentDirId;
    std::string _currentPath;
    
    void EnsureConnection();

public:
    // Constructor and destructor
    MTPDevice(const std::string &device_id);
    virtual ~MTPDevice();

    // Connection management
    bool Connect();
    void Disconnect();
    bool IsConnected() const { return _connected; }
    std::string GetDeviceId() const { return _device_id; }
    
    // Error mapping
    static int Str2Errno(const std::string &mtpError);
    
    // MTP-specific methods
    bool InitializeMTP();
    void CleanupMTP();
    LIBMTP_mtpdevice_t* GetDevice() const { return _device; }
    LIBMTP_devicestorage_t* GetStorage() const { return _storage; }
    
    // Device properties access
    std::string GetFriendlyName() const { return _friendlyName; }
    std::string GetManufacturer() const { return _manufacturer; }
    std::string GetModel() const { return _model; }
    std::string GetSerialNumber() const { return _serialNumber; }
    
    // State management
    uint32_t GetCurrentStorageId() const { return _currentStorageId; }
    uint32_t GetCurrentDirId() const { return _currentDirId; }
    std::string GetCurrentPath() const { return _currentPath; }
    
    // State setters
    void SetFriendlyName(const std::string& name) { _friendlyName = name; }
    void SetCurrentStorage(uint32_t storageId, const std::string& storageName);
    void SetCurrentDir(uint32_t dirId, const std::string& dirName);
    void NavigateUp();
    void NavigateToRoot();
    
    // File operations
    int CreateMTPDirectory(const std::string& dirName);
    int DeleteMTPFile(uint32_t objectId);
    int DeleteMTPDirectory(uint32_t objectId);
};
