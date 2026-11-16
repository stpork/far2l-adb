#pragma once
#include "Types.h"

// Keep an existing destination recoverable until the whole replacement succeeds,
// including host fallback transfers. Backends identify objects by stable handles.
template<class Backend, class Action>
proto::Status ReplaceMtpObject(Backend& backend, const proto::ObjectEntry* existing,
                               const std::string& aside, Action action)
{
    if (!existing) return action();
    const proto::ObjectEntry original = *existing;
    auto saved = backend.Rename(original.handle, aside);
    if (!saved.ok) return saved;
    auto restore = [&]() {
        auto result = backend.Rename(original.handle, original.name);
        if (!result.ok) {
            return proto::Status::Failure(proto::ErrorCode::Io,
                result.message + "; original destination retained as " + aside);
        }
        return proto::OkStatus();
    };
    proto::Status result;
    try {
        result = action();
    } catch (...) {
        restore();
        throw;
    }
    if (!result.ok) {
        auto restored = restore();
        return restored.ok ? result : restored;
    }
    auto cleanup = backend.Delete(original.handle, /*recursive=*/true);
    if (!cleanup.ok) {
        return proto::Status::Failure(cleanup.code,
            cleanup.message + "; old destination retained as " + aside);
    }
    return result;
}
