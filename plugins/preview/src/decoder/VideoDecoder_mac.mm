#ifdef __APPLE__

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreGraphics/CoreGraphics.h>

#include "VideoDecoder.h"
#include "../PreviewLog.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace {

class MacVideoStoryboardDecoder final : public ImageDecoder {
public:
	bool Decode(const std::string& path, Image& out, ImageDecodeInfo& info,
	            int maxPixelSize, const DecodeCancelFlag* cancel) override;
	bool CanHandle(const char* ext) const override;
	const char* Name() const override { return "AVFoundationVideo"; }
	bool SupportsDecodeScaling(const std::string&) const override { return true; }

private:
	static bool ExtractRGBFromCGImage(CGImageRef cgImage, int targetW, int targetH, Image& out);
};

bool MacVideoStoryboardDecoder::CanHandle(const char* ext) const
{
	return VideoHelpers::IsVideoExtension(ext);
}

bool MacVideoStoryboardDecoder::ExtractRGBFromCGImage(CGImageRef cgImage, int targetW, int targetH, Image& out)
{
	if (!cgImage || targetW <= 0 || targetH <= 0) return false;

	out.Resize(targetW, targetH, 3);
	CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
	if (!colorSpace) return false;

	CGContextRef ctx = CGBitmapContextCreate(nullptr, targetW, targetH, 8, targetW * 4,
	                                         colorSpace,
	                                         kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
	CGColorSpaceRelease(colorSpace);
	if (!ctx) return false;

	CGContextSetInterpolationQuality(ctx, kCGInterpolationMedium);
	CGContextDrawImage(ctx, CGRectMake(0, 0, targetW, targetH), cgImage);

	const uint8_t* rgba = static_cast<const uint8_t*>(CGBitmapContextGetData(ctx));
	if (!rgba) {
		CGContextRelease(ctx);
		return false;
	}

	for (int y = 0; y < targetH; ++y) {
		const uint8_t* srcRow = rgba + y * (targetW * 4);
		uint8_t* dstRow = out.Ptr(0, y);
		for (int x = 0; x < targetW; ++x) {
			dstRow[x * 3 + 0] = srcRow[x * 4 + 0];
			dstRow[x * 3 + 1] = srcRow[x * 4 + 1];
			dstRow[x * 3 + 2] = srcRow[x * 4 + 2];
		}
	}

	CGContextRelease(ctx);
	return true;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
bool MacVideoStoryboardDecoder::Decode(const std::string& path, Image& out, ImageDecodeInfo& info,
                                       int maxPixelSize, const DecodeCancelFlag* cancel)
{
	@autoreleasepool {
		NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
		if (!nsPath) return false;

		NSURL* url = [NSURL fileURLWithPath:nsPath];
		if (!url) return false;

		AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:@{AVURLAssetPreferPreciseDurationAndTimingKey: @NO}];
		if (!asset) return false;

		NSArray<AVAssetTrack*>* tracks = [asset tracksWithMediaType:AVMediaTypeVideo];
		if (!tracks || tracks.count == 0) {
			DBG("AVFoundation: no video tracks found in %s", path.c_str());
			return false;
		}

		AVAssetTrack* videoTrack = tracks[0];
		CGSize naturalSize = videoTrack.naturalSize;
		CGAffineTransform transform = videoTrack.preferredTransform;
		CGRect transformedRect = CGRectApplyAffineTransform(CGRectMake(0, 0, naturalSize.width, naturalSize.height), transform);
		int videoW = static_cast<int>(std::round(std::abs(transformedRect.size.width)));
		int videoH = static_cast<int>(std::round(std::abs(transformedRect.size.height)));
		if (videoW <= 0 || videoH <= 0) {
			videoW = 1920;
			videoH = 1080;
		}

		Float64 durationSec = CMTimeGetSeconds(asset.duration);
		if (durationSec <= 0.0 || std::isnan(durationSec)) {
			durationSec = 10.0;
		}

		// Calculate cell dimensions
		const int margin = 6;
		int cellW = 480;
		if (maxPixelSize > 0) {
			cellW = std::clamp((maxPixelSize - 4 * margin) / 3, 240, 640);
		}
		int cellH = std::max(120, static_cast<int>(std::round(static_cast<double>(cellW) * videoH / videoW)));

		AVAssetImageGenerator* gen = [AVAssetImageGenerator assetImageGeneratorWithAsset:asset];
		gen.appliesPreferredTrackTransform = YES;
		// 2-second keyframe tolerance allows instant hardware keyframe decoding
		gen.requestedTimeToleranceBefore = CMTimeMakeWithSeconds(2.0, 600);
		gen.requestedTimeToleranceAfter = CMTimeMakeWithSeconds(2.0, 600);
		gen.maximumSize = CGSizeMake(cellW, cellH);

		const int numFrames = 9;
		std::vector<Image> frames;
		std::vector<std::string> timecodes;
		frames.reserve(numFrames);
		timecodes.reserve(numFrames);

		for (int i = 0; i < numFrames; ++i) {
			if (DecodeCancelled(cancel)) return false;

			double targetSec = durationSec * (static_cast<double>(i + 1) / (numFrames + 1));
			CMTime reqTime = CMTimeMakeWithSeconds(targetSec, 600);
			CMTime actualTime = kCMTimeZero;
			NSError* err = nil;
			CGImageRef cgImage = [gen copyCGImageAtTime:reqTime actualTime:&actualTime error:&err];

			if (cgImage) {
				Image frame;
				if (ExtractRGBFromCGImage(cgImage, cellW, cellH, frame)) {
					frames.push_back(std::move(frame));
					double actualSec = CMTimeGetSeconds(actualTime);
					if (std::isnan(actualSec) || actualSec < 0.0) actualSec = targetSec;
					timecodes.push_back(VideoHelpers::FormatTimecode(actualSec));
				}
				CGImageRelease(cgImage);
			} else {
				// Duplicate previous frame or empty if decoding failed
				if (!frames.empty()) {
					frames.push_back(frames.back());
					timecodes.push_back(timecodes.back());
				}
			}
		}

		if (frames.empty()) {
			DBG("AVFoundation: failed to extract any frames from %s", path.c_str());
			return false;
		}

		// Ensure we have 9 frames for a complete 3x3 grid
		while (frames.size() < static_cast<size_t>(numFrames)) {
			frames.push_back(frames.back());
			timecodes.push_back(timecodes.back());
		}

		if (!VideoHelpers::ComposeGrid(frames, timecodes, cellW, cellH, margin, out)) {
			return false;
		}

		info.sourceWidth = out.Width();
		info.sourceHeight = out.Height();
		info.orientation = 1;
		info.fullResolution = true;
		return true;
	}
}
#pragma clang diagnostic pop

} // namespace

void CreateMacVideoDecoders(std::vector<std::shared_ptr<ImageDecoder>>& decoders)
{
	decoders.push_back(std::make_shared<MacVideoStoryboardDecoder>());
}

#endif // __APPLE__
