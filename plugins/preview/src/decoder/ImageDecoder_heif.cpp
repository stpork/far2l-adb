#include "ImageDecoder.h"
#include "../PreviewLog.h"
#include <algorithm>
#include <cstring>
#include <strings.h>
#include "external/stb_image_resize2.h"

#ifdef HAVE_HEIF
#include <libheif/heif.h>
#include <dlfcn.h>

namespace {
// Keep the library loaded until all its objects have been released. Retry on the
// next decode if it is absent, so installing it does not require restarting far2l.
class HeifApi {
    void* library = nullptr;
public:
#define HEIF_FUNCTIONS(X) \
    X(heif_context_alloc) \
    X(heif_context_free) \
    X(heif_context_read_from_file) \
    X(heif_context_get_primary_image_handle) \
    X(heif_image_handle_release) \
    X(heif_image_handle_get_width) \
    X(heif_image_handle_get_height) \
    X(heif_decode_image) \
    X(heif_image_release) \
    X(heif_image_get_plane_readonly)
#define DECLARE(name) decltype(&::name) name = nullptr;
    HEIF_FUNCTIONS(DECLARE)
#undef DECLARE
    HeifApi()
    {
#ifdef __APPLE__
        const char* names[] = {"/opt/homebrew/opt/libheif/lib/libheif.1.dylib",
            "/usr/local/opt/libheif/lib/libheif.1.dylib", "libheif.1.dylib"};
#else
        const char* names[] = {"libheif.so.1"};
#endif
        for (const char* name : names) {
            library = dlopen(name, RTLD_LOCAL | RTLD_NOW);
            if (!library) continue;
#define LOAD(name) name = reinterpret_cast<decltype(name)>(dlsym(library, #name));
            HEIF_FUNCTIONS(LOAD)
#undef LOAD
            bool complete = true;
#define CHECK(name) complete = complete && name;
            HEIF_FUNCTIONS(CHECK)
#undef CHECK
            if (complete) return;
            dlclose(library);
            library = nullptr;
        }
    }
    ~HeifApi() { if (library) dlclose(library); }
    HeifApi(const HeifApi&) = delete;
    HeifApi& operator=(const HeifApi&) = delete;
    explicit operator bool() const { return library != nullptr; }
#undef HEIF_FUNCTIONS
};
} // namespace

class HeifImageDecoder : public ImageDecoder {
public:
	const char* Name() const override { return "libheif"; }

	bool CanHandle(const char* ext) const override
	{
		if (!ext) return false;
		return strcasecmp(ext, "heic") == 0 || strcasecmp(ext, "heif") == 0 || strcasecmp(ext, "avif") == 0;
	}

	bool Decode(const std::string& path, Image& out, ImageDecodeInfo& info,
	            int maxPixelSize, const DecodeCancelFlag* cancel) override
	{
		if (DecodeCancelled(cancel)) return false;
		DBG("Decoding via libheif: %s", path.c_str());
		info = {};

        HeifApi api;
        if (!api) return false;
        auto freeContext = [&api](heif_context* c) { if (c) api.heif_context_free(c); };
        auto freeHandle = [&api](heif_image_handle* h) { if (h) api.heif_image_handle_release(h); };
        auto freeImage = [&api](heif_image* i) { if (i) api.heif_image_release(i); };
        std::unique_ptr<heif_context, decltype(freeContext)> ctx(api.heif_context_alloc(), freeContext);
		if (!ctx) return false;

		heif_error error = api.heif_context_read_from_file(ctx.get(), path.c_str(), nullptr);
		if (error.code != heif_error_Ok) return false;

		heif_image_handle* raw_handle = nullptr;
		error = api.heif_context_get_primary_image_handle(ctx.get(), &raw_handle);
		if (error.code != heif_error_Ok) return false;
		std::unique_ptr<heif_image_handle, decltype(freeHandle)> handle(raw_handle, freeHandle);

		int width = api.heif_image_handle_get_width(handle.get());
		int height = api.heif_image_handle_get_height(handle.get());
		info.sourceWidth = width;
		info.sourceHeight = height;

		if (width <= 0 || height <= 0 || (uint64_t)width * height > kMaxImagePixels) return false;

		int targetWidth = width;
		int targetHeight = height;
		if (maxPixelSize > 0 && (width > maxPixelSize || height > maxPixelSize)) {
			float scale = (float)maxPixelSize / (float)std::max(width, height);
			targetWidth = std::max(1, (int)(width * scale));
			targetHeight = std::max(1, (int)(height * scale));
		}

		heif_image* raw_img = nullptr;
		error = api.heif_decode_image(handle.get(), &raw_img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr);
		if (error.code != heif_error_Ok) return false;
		std::unique_ptr<heif_image, decltype(freeImage)> img(raw_img, freeImage);

		if (DecodeCancelled(cancel)) return false;

		int stride;
		const uint8_t* data = api.heif_image_get_plane_readonly(img.get(), heif_channel_interleaved, &stride);

		if (!data || stride < width * 3) return false;

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
