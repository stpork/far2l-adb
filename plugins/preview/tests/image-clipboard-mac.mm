// Uses a private pasteboard: the user's clipboard is never changed by this test.
#include <cassert>
#include <cstdint>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "../src/decoder/external/stb_image_resize2.h"
#include "../src/ImageClipboard_mac.mm"

static void CheckPixels(NSPasteboard *pasteboard, NSString *type, const Image &expected)
{
	NSData *data = [pasteboard dataForType:type];
	assert(data);
	NSBitmapImageRep *bitmap = [NSBitmapImageRep imageRepWithData:data];
	assert(bitmap && bitmap.pixelsWide == expected.Width() && bitmap.pixelsHigh == expected.Height());
	for (int y = 0; y < expected.Height(); ++y) {
		for (int x = 0; x < expected.Width(); ++x) {
			NSUInteger pixel[4]{};
			[bitmap getPixel:pixel atX:x y:y];
			for (int c = 0; c < expected.BytesPerPixel(); ++c)
				assert(pixel[c] == *expected.Ptr(x, y, c));
		}
	}
}

int main()
{
	@autoreleasepool {
		NSPasteboard *pasteboard = [NSPasteboard pasteboardWithUniqueName];
		Image image(3, 2, 3);
		for (int y = 0; y < image.Height(); ++y)
			for (int x = 0; x < image.Width(); ++x)
				for (int c = 0; c < 3; ++c) *image.Ptr(x, y, c) = 20 + y * 80 + x * 10 + c;
		assert(CopyImageToPasteboard(image, pasteboard));
		CheckPixels(pasteboard, NSPasteboardTypePNG, image);
		CheckPixels(pasteboard, NSPasteboardTypeTIFF, image);
		assert(![pasteboard stringForType:NSPasteboardTypeString]);

		image.MirrorH();
		Image rotated;
		image.Rotate(rotated, true);
		assert(CopyImageToPasteboard(rotated, pasteboard));
		Image expected = rotated;
		rotated.Resize(); // The copy must survive closing/navigating the viewer.
		CheckPixels(pasteboard, NSPasteboardTypePNG, expected);
		CheckPixels(pasteboard, NSPasteboardTypeTIFF, expected);

		const NSInteger count = pasteboard.changeCount;
		assert(!CopyImageToPasteboard(Image(), pasteboard));
		assert(pasteboard.changeCount == count);
		CheckPixels(pasteboard, NSPasteboardTypePNG, expected);
		[pasteboard releaseGlobally];
	}
}
