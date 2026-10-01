#include <cstdint>
#include <cstring>
#include "Image.h"
#include "ImageClipboard.h"
#import <AppKit/AppKit.h>

static bool CopyImageToPasteboard(const Image &image, NSPasteboard *pasteboard)
{
	@autoreleasepool {
		@try {
			const int channels = image.BytesPerPixel();
			if (image.Width() <= 0 || image.Height() <= 0 || (channels != 3 && channels != 4))
				return false;
			const size_t stride = size_t(image.Width()) * channels;
			if (image.Size() != stride * size_t(image.Height())) return false;

			NSBitmapImageRep *bitmap = [[[NSBitmapImageRep alloc]
				initWithBitmapDataPlanes:nullptr pixelsWide:image.Width() pixelsHigh:image.Height()
				bitsPerSample:8 samplesPerPixel:channels hasAlpha:(channels == 4) isPlanar:NO
				colorSpaceName:NSDeviceRGBColorSpace bitmapFormat:NSBitmapFormatAlphaNonpremultiplied
				bytesPerRow:stride bitsPerPixel:channels * 8] autorelease];
			if (!bitmap || ![bitmap bitmapData]) return false;
			for (int y = 0; y < image.Height(); ++y) {
				std::memcpy([bitmap bitmapData] + size_t(y) * [bitmap bytesPerRow],
					image.Data(size_t(y) * stride), stride);
			}

			// Encode eagerly so navigation/closing the viewer cannot invalidate the copy.
			NSData *png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
			NSData *tiff = [bitmap representationUsingType:NSBitmapImageFileTypeTIFF properties:@{}];
			if (!png || !tiff) return false;
			NSPasteboardItem *item = [[[NSPasteboardItem alloc] init] autorelease];
			if (![item setData:png forType:NSPasteboardTypePNG]
					|| ![item setData:tiff forType:NSPasteboardTypeTIFF]) return false;
			[pasteboard clearContents];
			return [pasteboard writeObjects:@[item]];
		} @catch (NSException *exception) {
			return false;
		}
	}
}

bool CopyImageToClipboard(const Image &image)
{
	@autoreleasepool {
		return CopyImageToPasteboard(image, [NSPasteboard generalPasteboard]);
	}
}
