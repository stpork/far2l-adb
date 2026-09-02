#ifndef FAR_SETTINGS_H
#define FAR_SETTINGS_H
#include <string>
#include <stdint.h>
#include <cstdint>
#include <atomic>
#include "lng.h"

#ifndef PREVIEW_NATIVE_DEFAULT
#define PREVIEW_NATIVE_DEFAULT 0
#endif
#ifndef PREVIEW_HAS_NATIVE
#define PREVIEW_HAS_NATIVE 0
#endif

class Settings
{
public:
	enum DefaultScale {
		FIT_AUTO = 0,
		FIT_WIDTH,
		FIT_HEIGHT,
		FIT_ORIGINAL,

		INVALID_SCALE_EDGE_VALUE
	};

private:
	std::string _ini_path;
	std::atomic<uint32_t> _generation{0};
	std::atomic<DefaultScale> _default_scale{FIT_AUTO};
	std::atomic<bool> _use_orientation{true};
	std::atomic<bool> _open_by_enter{true};
	std::atomic<bool> _open_by_cpgdn{true};
	std::atomic<bool> _open_in_qv{true};
	std::atomic<bool> _open_in_fv{true};
	std::atomic<bool> _autofit_on_rotate{false};
	std::atomic<bool> _fast_transforms{true};
	std::atomic<bool> _compact_frame{false};
	std::atomic<bool> _enabled{true};
	std::atomic<bool> _native_implementation{PREVIEW_NATIVE_DEFAULT != 0};
	std::string _image_masks;

public:
	Settings();
	const wchar_t *Msg(int msgId);
	void ConfigurationDialog();

	bool Enabled() const { return _enabled.load(std::memory_order_relaxed); }
	bool NativeImplementation() const { return _native_implementation.load(std::memory_order_relaxed); }
	bool UseOrientation() const { return _use_orientation.load(std::memory_order_relaxed); }
	bool OpenByEnter() const { return _open_by_enter.load(std::memory_order_relaxed); }
	bool OpenByCtrlPgDn() const { return _open_by_cpgdn.load(std::memory_order_relaxed); }
	bool OpenInQV() const { return _open_in_qv.load(std::memory_order_relaxed); }
	bool OpenInFV() const { return _open_in_fv.load(std::memory_order_relaxed); }
	bool AutoFitOnRotate() const { return _autofit_on_rotate.load(std::memory_order_relaxed); }
	bool FastTransforms() const { return _fast_transforms.load(std::memory_order_relaxed); }
	bool CompactFrame() const { return _compact_frame.load(std::memory_order_relaxed); }

	DefaultScale GetDefaultScale() const { return _default_scale.load(std::memory_order_relaxed); }
	void SetDefaultScale(DefaultScale default_scale);

	bool MatchFile(const char *name) const;

	uint32_t SettingsGeneration() const { return _generation.load(std::memory_order_acquire); }
};

extern Settings g_settings;

#endif
