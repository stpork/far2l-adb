#include "ImageDecoder.h"
#include "ImageDecoder_tiff.h"
#include "VideoDecoder.h"
#include "../Settings.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>
#include <utility>

// Platform-specific function declarations
#if PREVIEW_HAS_NATIVE
void CreateMacDecoders(std::vector<std::shared_ptr<ImageDecoder>>& decoders);
#endif
void CreateCrossPlatformDecoders(std::vector<std::shared_ptr<ImageDecoder>>& decoders);
void CreateHeifDecoder(std::vector<std::shared_ptr<ImageDecoder>>& decoders);
void CreateWebPDecoder(std::vector<std::shared_ptr<ImageDecoder>>& decoders);

namespace {
class FallbackImageDecoder final : public ImageDecoder {
	std::vector<std::shared_ptr<ImageDecoder>> _backends;
public:
	explicit FallbackImageDecoder(std::vector<std::shared_ptr<ImageDecoder>> backends)
		: _backends(std::move(backends)) {}

	bool Decode(const std::string& path, Image& out, ImageDecodeInfo& info,
	            int maxPixelSize, const DecodeCancelFlag* cancel) override
	{
		const std::string ext = ExifHelpers::GetExtension(path);

		// Fast path: try backends that declare support for this extension
		for (const auto& backend : _backends) {
			if (DecodeCancelled(cancel)) return false;
			if (backend && backend->CanHandle(ext.c_str()) &&
			    backend->Decode(path, out, info, maxPixelSize, cancel)) return true;
		}

		// Fallback: try remaining backends (handles misnamed extensions, e.g. webp/heic saved with .jpg)
		for (const auto& backend : _backends) {
			if (DecodeCancelled(cancel)) return false;
			if (backend && !backend->CanHandle(ext.c_str()) &&
			    backend->Decode(path, out, info, maxPixelSize, cancel)) return true;
		}

		return false;
	}

	bool CanHandle(const char* ext) const override
	{
		for (const auto& backend : _backends) {
			if (backend && backend->CanHandle(ext)) return true;
		}
		return false;
	}

	const char* Name() const override { return "FallbackImageDecoder"; }
	bool SupportsDecodeScaling(const std::string& path) const override
	{
		const std::string ext = ExifHelpers::GetExtension(path);
		for (const auto& backend : _backends) {
			if (backend && backend->CanHandle(ext.c_str())) return backend->SupportsDecodeScaling(path);
		}
		return false;
	}
};
}

std::vector<std::shared_ptr<ImageDecoder>> DecoderFactory::CreateDecoders()
{
	std::vector<std::shared_ptr<ImageDecoder>> decoders;
	std::vector<std::shared_ptr<ImageDecoder>> backends;

	if (g_settings.NativeImplementation()) {
#if PREVIEW_HAS_NATIVE
		CreateMacDecoders(backends);
		CreateMacVideoDecoders(backends);
#endif
	}
	CreateCrossPlatformDecoders(backends);
	CreateHeifDecoder(backends);
	CreateWebPDecoder(backends);
	CreateTiffDecoder(backends);
	CreateCrossPlatformVideoDecoders(backends);

	decoders.push_back(std::make_shared<FallbackImageDecoder>(std::move(backends)));
	return decoders;
}

// Decoder list is rebuilt only when settings change (generation counter).
// Mutex protects concurrent calls from main thread and ImageAtQV background thread.
static std::vector<std::shared_ptr<ImageDecoder>> s_decoders;
static uint32_t s_last_generation = UINT32_MAX; // force build on first use
static std::mutex s_decoders_mutex;

std::shared_ptr<ImageDecoder> DecoderFactory::FindDecoder(const std::string& path)
{
	std::lock_guard<std::mutex> lock(s_decoders_mutex);

	uint32_t gen = g_settings.SettingsGeneration();
	if (s_last_generation != gen) {
		s_decoders = CreateDecoders();
		s_last_generation = gen;
	}

	std::string ext = ExifHelpers::GetExtension(path);

	for (auto& decoder : s_decoders) {
		if (decoder && decoder->CanHandle(ext.c_str())) {
			return decoder;
		}
	}

	if (!s_decoders.empty()) {
		return s_decoders.front();
	}

	return nullptr;
}
