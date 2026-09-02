#ifdef __APPLE__

#include "ImageDecoder.h"
#include "../PreviewLog.h"
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <vector>
#include <algorithm>

#include <ImageIO/ImageIO.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>
#include <ImageIO/CGImageProperties.h>
#include <Accelerate/Accelerate.h>  // vImage for fast ARGB→RGB

// ============================================================================
// macOS ImageIO Decoder - optimized with thumbnail decode+scale
// ============================================================================
class MacOSImageDecoder : public ImageDecoder {
public:
	bool Decode(const std::string& path, Image& out, ImageDecodeInfo& info,
	            int maxPixelSize, const DecodeCancelFlag* cancel = nullptr) override;
	bool CanHandle(const char* ext) const override;
	const char* Name() const override { return "ImageIO"; }
	bool SupportsDecodeScaling(const std::string&) const override { return true; }

private:
	static CGImageRef LoadScaledImage(CGImageSourceRef src, int maxPixelSize, int sourceMaxSide);
	static bool ExtractRGB(CGImageRef image, Image& out);
};

bool MacOSImageDecoder::CanHandle(const char* ext) const
{
	if (!ext || !*ext) return false;

	const char* supported[] = {
		"jpg", "jpeg", "png", "gif", "webp", "heic", "heif", "tiff", "tif",
		"bmp", "ico", "cur", "xbm", "tga", "psd", "raw", "cr2", "nef", "dng",
		nullptr
	};

	for (const char** p = supported; *p; ++p) {
		if (strcasecmp(ext, *p) == 0) {
			return true;
		}
	}
	return false;
}

static int GetImagePropertyInt(CFDictionaryRef properties, CFStringRef key, int fallback)
{
	if (!properties) return fallback;
	CFNumberRef value = static_cast<CFNumberRef>(CFDictionaryGetValue(properties, key));
	int out = fallback;
	if (value && CFGetTypeID(value) == CFNumberGetTypeID()) {
		CFNumberGetValue(value, kCFNumberIntType, &out);
	}
	return out;
}

CGImageRef MacOSImageDecoder::LoadScaledImage(CGImageSourceRef src, int maxPixelSize, int sourceMaxSide)
{
	CGImageRef img = nullptr;

	if (maxPixelSize > 0 && maxPixelSize < sourceMaxSide) {
		// Use thumbnail API for fast decode + scale in one pass
		int maxSide = maxPixelSize;
		CFNumberRef maxSizeNum = CFNumberCreate(nullptr, kCFNumberIntType, &maxSide);

		const void* keys[] = {
			kCGImageSourceCreateThumbnailFromImageAlways,
			kCGImageSourceThumbnailMaxPixelSize,
			kCGImageSourceCreateThumbnailWithTransform,
			kCGImageSourceShouldCacheImmediately
		};
		const void* values[] = {
			kCFBooleanTrue,
			maxSizeNum,
			kCFBooleanFalse, // Common renderer applies EXIF orientation
			kCFBooleanFalse  // Don't cache thumbnails immediately to save memory/time
		};

		CFDictionaryRef options = CFDictionaryCreate(
			nullptr, keys, values, 4,
			&kCFTypeDictionaryKeyCallBacks,
			&kCFTypeDictionaryValueCallBacks
		);
		CFRelease(maxSizeNum);

		img = CGImageSourceCreateThumbnailAtIndex(src, 0, options);
		CFRelease(options);
	}


	if (!img) {
		// Fallback: full decode (no scaling)
		img = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
	}

	return img;
}

bool MacOSImageDecoder::ExtractRGB(CGImageRef image, Image& out)
{
	size_t width = CGImageGetWidth(image);
	size_t height = CGImageGetHeight(image);

	vImage_Buffer srcBuf{};
	vImage_CGImageFormat format = {
		.bitsPerComponent = 8,
		.bitsPerPixel = 32,
		.colorSpace = nullptr, // Default to sRGB
		.bitmapInfo = kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Big,
		.version = 0,
		.decode = nullptr,
		.renderingIntent = kCGRenderingIntentDefault
	};

	vImage_Error err = vImageBuffer_InitWithCGImage(&srcBuf, &format, nullptr, image, kvImageNoFlags);
	if (err != kvImageNoError) {
		DBG("vImageBuffer_InitWithCGImage failed: %ld", (long)err);
		return false;
	}

	// RAII wrapper to ensure memory is freed even if we add early returns later
	struct VImageBufferAutoFree {
		vImage_Buffer* buf;
		~VImageBufferAutoFree() { if (buf && buf->data) free(buf->data); }
	} autoFree{&srcBuf};

	// Allocate output buffer (3 bytes per pixel for RGB)
	out.Resize(width, height, 3);

	vImage_Buffer dstBuf = {
		.data = (void*)out.Data(),
		.height = (vImagePixelCount)height,
		.width = (vImagePixelCount)width,
		.rowBytes = width * 3
	};

	// Convert ARGB to RGB
	err = vImageConvert_ARGB8888toRGB888(&srcBuf, &dstBuf, kvImageNoFlags);

	return err == kvImageNoError;
}

bool MacOSImageDecoder::Decode(const std::string& path, Image& out, ImageDecodeInfo& info,
                               int maxPixelSize, const DecodeCancelFlag* cancel)
{
	if (DecodeCancelled(cancel)) return false;
	DBG("Decoding via Native ImageIO: %s", path.c_str());
	info = {};

	CFURLRef url = CFURLCreateFromFileSystemRepresentation(
		nullptr, reinterpret_cast<const UInt8*>(path.data()), path.size(), false);
	if (!url) return false;
	CGImageSourceRef source = CGImageSourceCreateWithURL(url, nullptr);
	CFRelease(url);
	if (!source) return false;

	CFDictionaryRef properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr);
	info.sourceWidth = GetImagePropertyInt(properties, kCGImagePropertyPixelWidth, 0);
	info.sourceHeight = GetImagePropertyInt(properties, kCGImagePropertyPixelHeight, 0);
	info.orientation = GetImagePropertyInt(properties, kCGImagePropertyOrientation, 1);
	if (properties) CFRelease(properties);
	if (info.orientation < 1 || info.orientation > 8) info.orientation = 1;
	if (info.sourceWidth <= 0 || info.sourceHeight <= 0 ||
	    (uint64_t)info.sourceWidth * info.sourceHeight > kMaxImagePixels) {
		CFRelease(source);
		return false;
	}

	// Load image with optional scaling
	CGImageRef image = LoadScaledImage(source, maxPixelSize,
	                                  std::max(info.sourceWidth, info.sourceHeight));
	CFRelease(source);
	if (!image) {
		DBG("Native ImageIO failed to load image: %s", path.c_str());
		return false;
	}

	// Extract RGB data
	bool success = !DecodeCancelled(cancel) && ExtractRGB(image, out) && !DecodeCancelled(cancel);
	DBG("Native ImageIO extraction success=%d, size=%zux%zu", success, CGImageGetWidth(image), CGImageGetHeight(image));

	CGImageRelease(image);
	info.fullResolution = success && out.Width() == info.sourceWidth && out.Height() == info.sourceHeight;
	return success;
}

// ============================================================================
// Factory function
// ============================================================================
void CreateMacDecoders(std::vector<std::shared_ptr<ImageDecoder>>& decoders)
{
	decoders.push_back(std::make_shared<MacOSImageDecoder>());
}

#endif // __APPLE__
