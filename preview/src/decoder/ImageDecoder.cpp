#include "ImageDecoder.h"
#include "ImageDecoder_tiff.h"
#include "../Settings.h"
#include <algorithm>
#include <cctype>
#include <cstring>

// Platform-specific function declarations
#ifdef __APPLE__
void CreateMacDecoders(std::vector<std::unique_ptr<ImageDecoder>>& decoders);
#endif
void CreateCrossPlatformDecoders(std::vector<std::unique_ptr<ImageDecoder>>& decoders);
void CreateHeifDecoder(std::vector<std::unique_ptr<ImageDecoder>>& decoders);
void CreateWebPDecoder(std::vector<std::unique_ptr<ImageDecoder>>& decoders);

std::vector<std::unique_ptr<ImageDecoder>> DecoderFactory::CreateDecoders()
{
	std::vector<std::unique_ptr<ImageDecoder>> decoders;

	if (g_settings.NativeImplementation()) {
#ifdef __APPLE__
		CreateMacDecoders(decoders);
#else
		CreateCrossPlatformDecoders(decoders);
		CreateHeifDecoder(decoders);
		CreateWebPDecoder(decoders);
		CreateTiffDecoder(decoders);
#endif
	} else {
		CreateCrossPlatformDecoders(decoders);
		CreateHeifDecoder(decoders);
		CreateWebPDecoder(decoders);
		CreateTiffDecoder(decoders);
	}

	return decoders;
}

// Decoder list is rebuilt only when settings change (generation counter)
static std::vector<std::unique_ptr<ImageDecoder>> s_decoders;
static uint32_t s_last_generation = UINT32_MAX; // force build on first use

ImageDecoder* DecoderFactory::FindDecoder(const std::string& path)
{
	uint32_t gen = g_settings.SettingsGeneration();
	if (s_last_generation != gen) {
		s_decoders = CreateDecoders();
		s_last_generation = gen;
	}

	std::string ext = ExifHelpers::GetExtension(path);
	if (ext.empty()) {
		return nullptr;
	}

	for (auto& decoder : s_decoders) {
		if (decoder && decoder->CanHandle(ext.c_str())) {
			return decoder.get();
		}
	}

	return nullptr;
}
