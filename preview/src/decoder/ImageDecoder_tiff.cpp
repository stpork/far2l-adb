#include "ImageDecoder.h"
#include "ImageDecoder_tiff.h"

#ifdef HAVE_TIFF
#include <tiff.h>
#include <tiffio.h>
#include "external/stb_image_resize2.h"
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>

class TiffImageDecoder : public ImageDecoder {
public:
	const char* Name() const override { return "libtiff"; }

	bool CanHandle(const char* ext) const override
	{
		if (!ext) return false;
		return strcmp(ext, "tiff") == 0 || strcmp(ext, "tif") == 0;
	}

	bool Decode(const std::string& path, Image& out, int& orientation,
	            int maxPixelSize, std::atomic<bool>* cancel) override
	{
		if (cancel && *cancel) return false;
		orientation = ExifHelpers::ReadExifOrientation(path);

		TIFF* tif = TIFFOpen(path.c_str(), "r");
		if (!tif) return false;

		uint32_t image_width = 0, image_height = 0;
		if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &image_width) ||
		    !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &image_height) ||
		    image_width == 0 || image_height == 0) {
			TIFFClose(tif);
			return false;
		}

		if ((uint64_t)image_width * image_height > kMaxImagePixels) {
			TIFFClose(tif);
			return false;
		}

		int targetWidth = (int)image_width;
		int targetHeight = (int)image_height;
		if (maxPixelSize > 0 &&
		    ((int)image_width > maxPixelSize || (int)image_height > maxPixelSize)) {
			float scale = (float)maxPixelSize / (float)std::max(image_width, image_height);
			targetWidth  = (int)(image_width  * scale);
			targetHeight = (int)(image_height * scale);
		}

		// TIFFReadRGBAImageOriented stores RGBA as uint32 per pixel
		std::vector<uint32_t> rgba_buf(image_width * image_height);
		if (!TIFFReadRGBAImageOriented(tif, image_width, image_height,
		                               rgba_buf.data(), ORIENTATION_TOPLEFT, 0)) {
			TIFFClose(tif);
			return false;
		}
		TIFFClose(tif);

		out.Resize(targetWidth, targetHeight, 3);
		if (targetWidth != (int)image_width || targetHeight != (int)image_height) {
			stbir_resize_uint8_linear(
				(const uint8_t*)rgba_buf.data(), (int)image_width, (int)image_height, (int)image_width * 4,
				(uint8_t*)out.Data(), targetWidth, targetHeight, 0, STBIR_RGB);
		} else {
			const uint8_t* src = (const uint8_t*)rgba_buf.data();
			uint8_t*       dst = (uint8_t*)out.Data();
			for (uint32_t y = 0; y < image_height; ++y) {
				const uint8_t* src_row = src + y * image_width * 4;
				uint8_t*       dst_row = dst + y * image_width * 3;
				for (uint32_t x = 0; x < image_width; ++x) {
					dst_row[x * 3 + 0] = src_row[x * 4 + 0]; // R
					dst_row[x * 3 + 1] = src_row[x * 4 + 1]; // G
					dst_row[x * 3 + 2] = src_row[x * 4 + 2]; // B
				}
			}
		}
		return true;
	}
};
#endif // HAVE_TIFF

void CreateTiffDecoder(std::vector<std::unique_ptr<ImageDecoder>>& decoders)
{
#ifdef HAVE_TIFF
	decoders.push_back(std::make_unique<TiffImageDecoder>());
#endif
}
