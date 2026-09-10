#include "ImageDecoder.h"
#include "../PreviewLog.h"
#include <algorithm>
#include <cstring>
#include <strings.h>
#include <fstream>
#include <vector>

#ifdef HAVE_WEBP
#include <webp/decode.h>

class WebPImageDecoder : public ImageDecoder {
public:
	const char* Name() const override { return "libwebp"; }

	bool CanHandle(const char* ext) const override
	{
		if (!ext) return false;
		return strcasecmp(ext, "webp") == 0;
	}

	bool SupportsDecodeScaling(const std::string&) const override { return true; }

	bool Decode(const std::string& path, Image& out, ImageDecodeInfo& info,
	            int maxPixelSize, const DecodeCancelFlag* cancel) override
	{
		if (DecodeCancelled(cancel)) return false;
		DBG("Decoding via libwebp: %s", path.c_str());
		info = {};

		static constexpr std::streamsize kMaxFileBytes = 256LL * 1024 * 1024; // 256 MB

		std::ifstream file(path, std::ios::binary);
		if (!file.is_open()) return false;

		// Fast header check (32 bytes) before loading full file into memory
		uint8_t header[32];
		if (!file.read((char*)header, sizeof(header))) return false;

		int width = 0, height = 0;
		if (!WebPGetInfo(header, sizeof(header), &width, &height)) return false;
		if ((uint64_t)width * height > kMaxImagePixels) return false;

		file.seekg(0, std::ios::end);
		std::streamsize size = file.tellg();
		if (size < 0 || size > kMaxFileBytes) return false;
		file.seekg(0, std::ios::beg);

		std::vector<uint8_t> buffer(size);
		if (!file.read((char*)buffer.data(), size)) return false;
		info.sourceWidth = width;
		info.sourceHeight = height;

		if (DecodeCancelled(cancel)) return false;

		WebPDecoderConfig config;
		if (!WebPInitDecoderConfig(&config)) return false;

		int targetWidth = width;
		int targetHeight = height;
		if (maxPixelSize > 0 && (width > maxPixelSize || height > maxPixelSize)) {
			float scale = (float)maxPixelSize / (float)std::max(width, height);
			targetWidth = std::max(1, (int)(width * scale));
			targetHeight = std::max(1, (int)(height * scale));
			config.options.use_scaling = 1;
			config.options.scaled_width = targetWidth;
			config.options.scaled_height = targetHeight;
		}
		config.options.use_threads = 1;

		out.Resize(targetWidth, targetHeight, 3);
		if (out.Width() != targetWidth || out.Height() != targetHeight) return false;

		config.output.colorspace = MODE_RGB;
		config.output.is_external_memory = 1;
		config.output.u.RGBA.rgba = (uint8_t*)out.Data();
		config.output.u.RGBA.stride = targetWidth * 3;
		config.output.u.RGBA.size = (size_t)targetWidth * targetHeight * 3;

		VP8StatusCode status = WebPDecode(buffer.data(), buffer.size(), &config);
		WebPFreeDecBuffer(&config.output);

		if (status != VP8_STATUS_OK || DecodeCancelled(cancel)) {
			out.Resize();
			return false;
		}

		info.fullResolution = (out.Width() == info.sourceWidth && out.Height() == info.sourceHeight);
		return true;
	}
};
#endif // HAVE_WEBP

void CreateWebPDecoder(std::vector<std::shared_ptr<ImageDecoder>>& decoders)
{
#ifdef HAVE_WEBP
	decoders.push_back(std::make_shared<WebPImageDecoder>());
#endif
}
