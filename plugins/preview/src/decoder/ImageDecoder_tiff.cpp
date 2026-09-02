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
#include <cstring>

class TiffImageDecoder : public ImageDecoder {
public:
	const char* Name() const override { return "libtiff"; }

	bool CanHandle(const char* ext) const override
	{
		if (!ext) return false;
		return strcmp(ext, "tiff") == 0 || strcmp(ext, "tif") == 0;
	}

	bool Decode(const std::string& path, Image& out, ImageDecodeInfo& info,
	            int maxPixelSize, const DecodeCancelFlag* cancel) override
	{
		if (DecodeCancelled(cancel)) return false;
		info = {};

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
		info.sourceWidth = (int)image_width;
		info.sourceHeight = (int)image_height;

		int targetWidth = (int)image_width;
		int targetHeight = (int)image_height;
		if (maxPixelSize > 0 &&
		    ((int)image_width > maxPixelSize || (int)image_height > maxPixelSize)) {
			float scale = (float)maxPixelSize / (float)std::max(image_width, image_height);
			targetWidth  = std::max(1, (int)(image_width  * scale));
			targetHeight = std::max(1, (int)(image_height * scale));
		}

		// TIFFReadRGBAImageOriented stores RGBA as uint32 per pixel
		std::vector<uint32_t> rgba_buf(image_width * image_height);
		if (!TIFFReadRGBAImageOriented(tif, image_width, image_height,
		                               rgba_buf.data(), ORIENTATION_TOPLEFT, 0)) {
			TIFFClose(tif);
			return false;
		}
		TIFFClose(tif);
		if (DecodeCancelled(cancel)) return false;

		// libtiff's uint32 raster byte order is platform-dependent.  Convert via
		// TIFFGet* before resizing; treating the RGBA raster as packed RGB was
		// both channel-incorrect and used the wrong four-byte pixel stride.
		Image rgb((int)image_width, (int)image_height, 3);
		for (uint32_t y = 0; y < image_height; ++y) {
			uint8_t* dst_row = static_cast<uint8_t*>(rgb.Data()) + (size_t)y * image_width * 3;
			const uint32_t* src_row = rgba_buf.data() + (size_t)y * image_width;
			for (uint32_t x = 0; x < image_width; ++x) {
				dst_row[x * 3 + 0] = TIFFGetR(src_row[x]);
				dst_row[x * 3 + 1] = TIFFGetG(src_row[x]);
				dst_row[x * 3 + 2] = TIFFGetB(src_row[x]);
			}
			if (DecodeCancelled(cancel)) return false;
		}
		rgba_buf.clear();
		rgba_buf.shrink_to_fit();

		if (targetWidth != (int)image_width || targetHeight != (int)image_height) {
			out.Resize(targetWidth, targetHeight, 3);
			if (!stbir_resize_uint8_linear(
				static_cast<const uint8_t*>(rgb.Data()), (int)image_width, (int)image_height, 0,
				static_cast<uint8_t*>(out.Data()), targetWidth, targetHeight, 0, STBIR_RGB)) {
				return false;
			}
		} else {
			out = std::move(rgb);
		}
		info.fullResolution = (out.Width() == info.sourceWidth && out.Height() == info.sourceHeight);
		return true;
	}
};
#endif // HAVE_TIFF

void CreateTiffDecoder(std::vector<std::shared_ptr<ImageDecoder>>& decoders)
{
#ifdef HAVE_TIFF
	decoders.push_back(std::make_shared<TiffImageDecoder>());
#endif
}
