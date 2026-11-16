#pragma once
#include <string>

// Never replace a destination if it cannot first be saved. A failed move may
// have moved part of the source: retain both paths rather than deleting it.
template<class Device, class Action>
int ReplaceAdbObject(Device& device, const std::string& destination,
                     const std::string& aside, bool overwrite, Action action,
                     bool* may_retry = nullptr, bool moving = false)
{
    if (may_retry) *may_retry = false;
    if (overwrite) {
        const int saved = device.SaveRemoteBackup(destination, aside);
        if (saved) return saved;
    }
    int result;
    try {
        result = action();
    } catch (...) {
        if (overwrite) device.RestoreRemoteBackup(aside, destination, !moving);
        throw;
    }
    if (result) {
        if (overwrite) {
            const int restored = device.RestoreRemoteBackup(aside, destination, !moving);
            if (restored) return restored;
        }
        if (may_retry) *may_retry = !moving || overwrite;
        return result;
    }
    // rm -rf also handles an old destination of a different type than source.
    return overwrite ? device.DeleteDirectory(aside) : 0;
}
