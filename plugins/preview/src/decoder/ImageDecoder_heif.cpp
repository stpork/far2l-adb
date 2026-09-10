#include "ImageDecoder.h"
#include "../PreviewLog.h"
#include <algorithm>
#include <cstring>
#include <strings.h>
#include "external/stb_image_resize2.h"

#ifdef HAVE_HEIF
#include <libheif/heif.h>

class HeifImageDecoder : public ImageDecoder {
public:
	const char* Name() const override { return "libheif"; }

	bool CanHandle(const char* ext) const override
	{
		if (!ext) return false;
		return strcasecmp(ext, "heic") == 0 || strcasecmp(ext, "heif") == 0;
	}

	bool Decode(const std::string& path, Image& out, ImageDecodeInfo& info,
	            int maxPixelSize, const DecodeCancelFlag* cancel) override
	{
		if (DecodeCancelled(cancel)) return false;
		DBG("Decoding via libheif: %s", path.c_str());
		info = {};

		struct HeifContextDeleter { void operator()(heif_context* c) const { if (c) heif_context_free(c); } };
		struct HeifHandleDeleter { void operator()(heif_image_handle* h) const { if (h) heif_image_handle_release(h); } };
		struct HeifImageDeleter { void operator()(heif_image* i) const { if (i) heif_image_release(i); } };

		std::unique_ptr<heif_context, HeifContextDeleter> ctx(heif_context_alloc());
		if (!ctx) return false;

		heif_error error = heif_context_read_from_file(ctx.get(), path.c_str(), nullptr);
		if (error.code != heif_error_Ok) return false;

		heif_image_handle* raw_handle = nullptr;
		error = heif_context_get_primary_image_handle(ctx.get(), &raw_handle);
		if (error.code != heif_error_Ok) return false;
		std::unique_ptr<heif_image_handle, HeifHandleDeleter> handle(raw_handle);

		int width = heif_image_handle_get_width(handle.get());
		int height = heif_image_handle_get_height(handle.get());
		info.sourceWidth = width;
		info.sourceHeight = height;

		if ((uint64_t)width * height > kMaxImagePixels) return false;

		int targetWidth = width;
		int targetHeight = height;
		if (maxPixelSize > 0 && (width > maxPixelSize || height > maxPixelSize)) {
			float scale = (float)maxPixelSize / (float)std::max(width, height);
			targetWidth = std::max(1, (int)(width * scale));
			targetHeight = std::max(1, (int)(height * scale));
		}

		heif_image* raw_img = nullptr;
		error = heif_decode_image(handle.get(), &raw_img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr);
		if (error.code != heif_error_Ok) return false;
		std::unique_ptr<heif_image, HeifImageDeleter> img(raw_img);

		if (DecodeCancelled(cancel)) return false;

		int stride;
		const uint8_t* data = heif_image_get_plane_readonly(img.get(), heif_channel_interleaved, &stride);

		if (targetWidth != width || targetHeight != height) {
			out.Resize(targetWidth, targetHeight, 3);
			stbir_resize_uint8_linear(data, width, height, stride,
			                          (unsigned char*)out.Data(), targetWidth, targetHeight, 0,
			                          STBIR_RGB);
		} else {
			out.Resize(width, height, 3);
			if (stride == width * 3) {
				memcpy(out.Data(), data, width * height * 3);
			} else {
				for (int y = 0; y < height; ++y) {
					memcpy((uint8_t*)out.Data() + y * width * 3, data + y * stride, width * 3);
				}
			}
		}

		info.fullResolution = (out.Width() == info.sourceWidth && out.Height() == info.sourceHeight);
		return true;
	}
};
#endif // HAVE_HEIF

void CreateHeifDecoder(std::vector<std::shared_ptr<ImageDecoder>>& decoders)
{
#ifdef HAVE_HEIF
	decoders.push_back(std::make_shared<HeifImageDecoder>());
#endif
}
