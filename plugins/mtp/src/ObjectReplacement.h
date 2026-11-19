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

// Revalidate the preflight decision after downloading to the host. Devices can
// expose duplicate (or case-equivalent) names, so inspect every match.
template<class Backend, class Children, class Action>
proto::Status ReplaceApprovedMtpObject(Backend& backend, const Children& children,
                                      uint32_t storage, const std::string& name,
                                      uint32_t approved, const std::string& aside, Action action)
{
    const proto::ObjectEntry* existing = nullptr;
    for (const auto& child : children) {
        if (!backend.NamesEqual(storage, child.name, name)) continue;
        if (!approved || child.handle != approved || existing)
            return proto::Status::Failure(proto::ErrorCode::Busy, "Destination changed during transfer");
        existing = &child;
    }
    if (approved && !existing)
        return proto::Status::Failure(proto::ErrorCode::NotFound, "Approved destination disappeared");
    return ReplaceMtpObject(backend, existing, aside, action);
}

// MoveObject keeps the basename. If it must change, rename in the source folder
// first, protecting any sibling and restoring the original name on failure.
template<class Backend>
proto::Status MoveMtpObjectAs(Backend& backend, const proto::ObjectEntry& src,
                              uint32_t storage, uint32_t parent, const std::string& name)
{
    if (name == src.name) return backend.MoveObject(src.handle, storage, parent);
    auto siblings = backend.ListChildren(src.storage_id, src.parent);
    if (!siblings.ok) return proto::Status::Failure(siblings.code, siblings.message);
    for (const auto& sibling : siblings.value) {
        if (sibling.handle != src.handle && backend.NamesEqual(src.storage_id, sibling.name, name))
            return proto::Status::Failure(proto::ErrorCode::Unsupported, "Rename requires host staging");
    }
    auto renamed = backend.Rename(src.handle, name);
    if (!renamed.ok) return renamed;
    auto moved = backend.MoveObject(src.handle, storage, parent);
    if (!moved.ok) {
        auto restored = backend.Rename(src.handle, src.name);
        if (!restored.ok)
            return proto::Status::Failure(proto::ErrorCode::Io,
                restored.message + "; source retained as " + name);
    }
    return moved;
}
