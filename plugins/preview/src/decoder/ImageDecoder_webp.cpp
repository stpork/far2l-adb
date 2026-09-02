#include "ImageDecoder.h"
#include "../PreviewLog.h"
#include <algorithm>
#include <cstring>
#include <strings.h>
#include <fstream>
#include <vector>
#include "external/stb_image_resize2.h"

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

	bool Decode(const std::string& path, Image& out, ImageDecodeInfo& info,
	            int maxPixelSize, const DecodeCancelFlag* cancel) override
	{
		if (DecodeCancelled(cancel)) return false;
		DBG("Decoding via libwebp: %s", path.c_str());
		info = {};

		static constexpr std::streamsize kMaxFileBytes = 256LL * 1024 * 1024; // 256 MB

		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file.is_open()) return false;
		std::streamsize size = file.tellg();
		if (size < 0 || size > kMaxFileBytes) return false;
		file.seekg(0, std::ios::beg);

		std::vector<uint8_t> buffer(size);
		if (!file.read((char*)buffer.data(), size)) return false;

		// Pre-check dimensions before full decode
		int width, height;
		if (!WebPGetInfo(buffer.data(), buffer.size(), &width, &height)) return false;
		if ((uint64_t)width * height > kMaxImagePixels) return false;
		info.sourceWidth = width;
		info.sourceHeight = height;

		uint8_t* data = WebPDecodeRGB(buffer.data(), buffer.size(), &width, &height);
		if (!data) return false;
		if (DecodeCancelled(cancel)) {
			WebPFree(data);
			return false;
		}

		int targetWidth = width;
		int targetHeight = height;
		if (maxPixelSize > 0 && (width > maxPixelSize || height > maxPixelSize)) {
			float scale = (float)maxPixelSize / (float)std::max(width, height);
			targetWidth = std::max(1, (int)(width * scale));
			targetHeight = std::max(1, (int)(height * scale));
		}

		if (targetWidth != width || targetHeight != height) {
			out.Resize(targetWidth, targetHeight, 3);
			stbir_resize_uint8_linear(data, width, height, width * 3,
			                          (unsigned char*)out.Data(), targetWidth, targetHeight, 0,
			                          STBIR_RGB);
		} else {
			out.Resize(width, height, 3);
			memcpy(out.Data(), data, width * height * 3);
		}

		WebPFree(data);
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
