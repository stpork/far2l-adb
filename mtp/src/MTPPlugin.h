#pragma once

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "farplug-wide.h"

#include "LibMtpBackend.h"

struct ProgressState;

extern PluginStartupInfo g_Info;
extern FarStandardFunctions g_FSF;

class MTPPlugin {
public:
    MTPPlugin(const wchar_t* path = nullptr, bool path_is_standalone_config = false, int op_mode = 0);
    ~MTPPlugin();

    int GetFindData(PluginPanelItem** panel_items, int* items_number, int op_mode);
    void FreeFindData(PluginPanelItem* panel_items, int items_number);
    void GetOpenPluginInfo(OpenPluginInfo* info);
    int SetDirectory(const wchar_t* dir, int op_mode);
    int ProcessKey(int key, unsigned int control_state);
    int ProcessEvent(int event, void* param);

    int MakeDirectory(const wchar_t** name, int op_mode);
    int DeleteFiles(PluginPanelItem* panel_item, int items_number, int op_mode);
    int GetFiles(PluginPanelItem* panel_item, int items_number, int move, const wchar_t** dest_path, int op_mode);
    int PutFiles(PluginPanelItem* panel_item, int items_number, int move, const wchar_t* src_path, int op_mode);
    int Execute(PluginPanelItem* panel_item, int items_number, int op_mode);

    static PluginStartupInfo* GetInfo();

private:
    enum class ViewMode {
        Devices,
        Storages,
        Objects,
    };

    struct DeviceBind {
        proto::UsbDeviceCandidate candidate;
    };

    PluginPanelItem MakePanelItem(const std::string& name,
                                  bool is_dir,
                                  uint64_t size,
                                  uint64_t mtime_epoch,
                                  uint32_t object_id,
                                  uint32_t storage_id,
                                  uint32_t packed_device_id,
                                  const std::string& description = std::string(),
                                  uint64_t ctime_epoch = 0,
                                  uint32_t file_attributes = 0,
                                  uint32_t unix_mode = 0) const;
    PluginPanelItem MakeObjectPanelItem(const proto::ObjectEntry& entry,
                                        uint32_t packed_device_id) const;
    static std::string BuildObjectDescription(const proto::ObjectEntry& entry);
    void UpdateObjectsPanelTitle();

    bool GetSelectedPanelUserData(std::string& out) const;
    bool GetSelectedPanelFileName(std::string& out) const;
    bool EnsureConnected(const std::string& device_key);

    int ListDevices(PluginPanelItem** panel_items, int* items_number);
    int ListStorages(PluginPanelItem** panel_items, int* items_number);
    int ListObjects(PluginPanelItem** panel_items, int* items_number);

    bool ParseStorageToken(const std::string& token, uint32_t& storage_id) const;
    bool ParseObjectToken(const std::string& token, uint32_t& handle) const;
    bool ResolvePanelToken(const PluginPanelItem& item, std::string& token) const;
    bool EnterByToken(const std::string& token);

    int MapErrorToErrno(const proto::Status& st) const;
    int MapErrorToErrno(proto::ErrorCode code) const;
    void SetErrorFromStatus(const proto::Status& st) const;
    bool PromptInput(const wchar_t* title,
                     const wchar_t* prompt,
                     const wchar_t* history_name,
                     const std::string& initial_value,
                     std::string& out) const;
    void RefreshPanel() const;
    bool RenameSelectedItem();
    bool EnterSelectedItem();
    bool ExecuteSelected(int op_mode);
    bool CrossPanelCopyMoveSameDevice(bool move);
    // Shift+F5 in-place copy. Single-select prompts new name; multi
    // auto-suffixes ".copy". Falls back host-mediated on device refusal.
    bool ShiftF5CopyInPlace();

    // Recursive size for Overwrite-dialog's folder column (libmtp returns
    // 0 for folder size). Depth-bounded; returns partial sum on early out.
    uint64_t ComputeRecursiveSize(uint32_t storage_id, uint32_t handle,
                                  int depth = 0);

    // Resolve user-typed path ("../foo", "Internal/Temp/x") into
    // (storage_id, parent, basename). ok=false on unresolvable input.
    struct PathResolution {
        bool ok = false;
        uint32_t storage_id = 0;
        uint32_t parent = 0;
        std::string basename;
    };
    // src_name supplies basename on folder-only input. base_* anchors
    // resolution (active panel default; dst panel for cross-panel F5/F6).
    PathResolution ResolveNewNamePath(const std::string& input,
                                      const std::string& src_name = std::string(),
                                      bool auto_create_dirs = true,
                                      uint32_t base_storage_id = 0,
                                      uint32_t base_parent = 0,
                                      const std::string& base_storage_name = std::string());

    // Case-fold scan on FAT-class storages (catches "Test.txt" /
    // "test.txt"); strict find on ext4.
    const proto::ObjectEntry* FindExistingByName(
        const std::map<std::string, proto::ObjectEntry>& m,
        const std::string& name,
        uint32_t storage_id) const;

    // "<storage>/<dir>/<dir>/" prefix for ShiftF5/F6 prompt prefills.
    std::string BuildPanelRelativePath() const;

    // Folder-only variant of ResolveNewNamePath (no basename) for
    // F5/F6 destination prompts.
    struct DirResolution {
        bool ok = false;
        uint32_t storage_id = 0;
        uint32_t parent = 0;
    };
    DirResolution ResolveDestinationFolder(const std::string& input);

    // Device-side primitives + host fallback for items targeting an
    // in-device folder; reached when F5/F6 destination resolves on-device.
    int InDeviceCopyMoveTo(PluginPanelItem* panel_item, int items_number,
                           bool move, uint32_t dst_storage, uint32_t dst_parent);

    proto::Status DownloadRecursive(uint32_t storage_id,
                                    uint32_t handle,
                                    const std::string& local_root,
                                    proto::CancellationToken token,
                                    uint64_t& downloaded,
                                    uint64_t& total,
                                    ProgressState* prog);

    // Recursive upload: MakeDirectory(remote_name) then walk + recurse.
    proto::Status UploadRecursive(const std::string& local_dir,
                                  const std::string& remote_name,
                                  uint32_t storage_id,
                                  uint32_t parent,
                                  proto::CancellationToken token,
                                  proto::CancellationSource* cancel_src,
                                  ProgressState* prog,
                                  uint64_t* bytes_done_so_far);

    // Host-staged copy fallback (download → upload) for devices that
    // refuse Copy_Object. Owns its progress dialog.
    proto::Status ManualCopyViaHost(uint32_t src_handle,
                                    const proto::ObjectEntry& src_stat,
                                    uint32_t dst_storage_id,
                                    uint32_t dst_parent,
                                    const std::string& new_name);

    // Inline variant: caller-supplied ProgressState/cancel, so a batch
    // shares one progress dialog instead of one-modal-per-item.
    proto::Status ManualCopyViaHostInline(uint32_t src_handle,
                                          const proto::ObjectEntry& src_stat,
                                          uint32_t dst_storage_id,
                                          uint32_t dst_parent,
                                          const std::string& new_name,
                                          ProgressState& st,
                                          proto::CancellationToken token,
                                          proto::CancellationSource* cancel_src);

    // Cross-panel host-fallback batch when CopyObject/MoveObject refused.
    // move=true deletes source after each successful host-staged copy.
    struct HostFallbackItem {
        uint32_t src_handle;
        proto::ObjectEntry src_stat;
        uint32_t dst_storage_id;
        uint32_t dst_parent;
        std::string dst_name;
    };
    proto::Status RunHostFallbackBatch(const std::vector<HostFallbackItem>& items,
                                       bool move,
                                       size_t* out_ok_count = nullptr);

    std::wstring _standalone_config;
    std::wstring _panel_title;
    // Backing for OpenPluginInfo::CurDir; pointer must outlive the call.
    mutable std::wstring _cur_dir_buf;

    ViewMode _view_mode = ViewMode::Devices;
    std::string _current_device_key;
    uint32_t _current_storage_id = 0;
    uint32_t _current_parent = 0;
    std::string _current_device_name;     // friendly: libmtp description, fallback product
    std::string _current_device_serial;   // USB iSerialNumber — primary device ID
    std::string _current_storage_name;    // panel label: "Internal" / "External" / "Storage"
    std::vector<std::string> _dir_stack;

    std::unordered_map<std::string, DeviceBind> _device_binds;
    std::unordered_map<std::string, std::string> _name_token_index;

    // Shared registry-managed backend; second panel reuses the libmtp
    // session instead of re-claiming the USB interface.
    std::shared_ptr<proto::LibMtpBackend> _backend;

    mutable std::string _last_error_message;
    std::wstring _last_made_dir;
};
