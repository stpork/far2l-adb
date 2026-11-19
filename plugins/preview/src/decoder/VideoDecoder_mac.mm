#ifdef __APPLE__

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Accelerate/Accelerate.h>

#include "VideoDecoder.h"
#include "../PreviewLog.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <array>
#include <chrono>
#include <condition_variable>

namespace {

using Clock = std::chrono::steady_clock;

struct LoadState {
	std::mutex mutex;
	std::condition_variable changed;
	size_t completed = 0;

	bool Wait(size_t count, Clock::time_point deadline, const DecodeCancelFlag* cancel)
	{
		std::unique_lock<std::mutex> lock(mutex);
		while (completed < count) {
			if (DecodeCancelled(cancel) || Clock::now() >= deadline) return false;
			changed.wait_until(lock, std::min(deadline, Clock::now() + std::chrono::milliseconds(50)));
		}
		return !DecodeCancelled(cancel);
	}
};

static bool LoadKeys(id<AVAsynchronousKeyValueLoading> object, NSArray<NSString*>* keys,
                     Clock::time_point deadline, const DecodeCancelFlag* cancel)
{
	auto state = std::make_shared<LoadState>();
	[object loadValuesAsynchronouslyForKeys:keys completionHandler:^{
		std::lock_guard<std::mutex> lock(state->mutex);
		state->completed = 1;
		state->changed.notify_one();
	}];
	if (!state->Wait(1, deadline, cancel)) return false;
	for (NSString* key in keys) {
		if ([object statusOfValueForKey:key error:nullptr] != AVKeyValueStatusLoaded) return false;
	}
	return true;
}

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

	vImage_Buffer srcBuf = {
		const_cast<void*>(static_cast<const void*>(rgba)),
		static_cast<vImagePixelCount>(targetH),
		static_cast<vImagePixelCount>(targetW),
		static_cast<size_t>(targetW * 4)
	};
	vImage_Buffer dstBuf = {
		out.Data(),
		static_cast<vImagePixelCount>(targetH),
		static_cast<vImagePixelCount>(targetW),
		static_cast<size_t>(targetW * 3)
	};
	vImage_Error err = vImageConvert_RGBA8888toRGB888(&srcBuf, &dstBuf, kvImageNoFlags);
	CGContextRelease(ctx);
	return (err == kvImageNoError);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
bool MacVideoStoryboardDecoder::Decode(const std::string& path, Image& out, ImageDecodeInfo& info,
                                       int maxPixelSize, const DecodeCancelFlag* cancel)
{
	const auto deadline = Clock::now() + std::chrono::seconds(15);
	if (DecodeCancelled(cancel)) return false;
	@autoreleasepool {
		NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
		if (!nsPath) return false;

		NSURL* url = [NSURL fileURLWithPath:nsPath];
		if (!url) return false;

		AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:@{AVURLAssetPreferPreciseDurationAndTimingKey: @NO}];
		if (!asset || !LoadKeys(asset, @[@"tracks", @"duration"], deadline, cancel)) {
			[asset cancelLoading];
			return false;
		}

		NSArray<AVAssetTrack*>* tracks = [asset tracksWithMediaType:AVMediaTypeVideo];
		if (!tracks || tracks.count == 0) {
			DBG("AVFoundation: no video tracks found in %s", path.c_str());
			return false;
		}

		AVAssetTrack* videoTrack = tracks[0];
		if (!LoadKeys(videoTrack, @[@"naturalSize", @"preferredTransform"], deadline, cancel)) {
			[asset cancelLoading];
			return false;
		}
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
		if (CMTIME_IS_INVALID(asset.duration) || CMTIME_IS_INDEFINITE(asset.duration) ||
		    std::isnan(durationSec) || std::isinf(durationSec) || durationSec <= 0.0) {
			durationSec = 10.0;
		}

		// Calculate cell dimensions bounded within maxCellDim box for both portrait & landscape
		const int margin = 6;
		int maxCellDim = 480;
		if (maxPixelSize > 0) {
			maxCellDim = std::clamp((maxPixelSize - 4 * margin) / 3, 180, 640);
		}
		int cellW = maxCellDim;
		int cellH = maxCellDim;
		if (videoW >= videoH) {
			cellW = maxCellDim;
			cellH = std::max(60, static_cast<int>(std::round(static_cast<double>(cellW) * videoH / videoW)));
		} else {
			cellH = maxCellDim;
			cellW = std::max(60, static_cast<int>(std::round(static_cast<double>(cellH) * videoW / videoH)));
		}

		AVAssetImageGenerator* gen = [AVAssetImageGenerator assetImageGeneratorWithAsset:asset];
		gen.appliesPreferredTrackTransform = YES;
		// Dynamic keyframe tolerance: avoids keyframe collisions on short videos while remaining fast
		Float64 maxTol = std::max(0.05, std::min(2.0, durationSec / 18.0));
		gen.requestedTimeToleranceBefore = CMTimeMakeWithSeconds(maxTol, 600);
		gen.requestedTimeToleranceAfter = CMTimeMakeWithSeconds(maxTol, 600);
		gen.maximumSize = CGSizeMake(cellW, cellH);

		const int numFrames = 9;
		std::vector<Image> frames;
		std::vector<std::string> timecodes;
		frames.reserve(numFrames);
		timecodes.reserve(numFrames);

		struct FrameBatch : LoadState {
			std::array<CGImageRef, numFrames> images{};
			std::array<CMTime, numFrames> requested{}, actual{};
			std::array<bool, numFrames> done{};
			~FrameBatch() { for (auto image : images) if (image) CGImageRelease(image); }
		};
		auto batch = std::make_shared<FrameBatch>();
		NSMutableArray<NSValue*>* times = [NSMutableArray arrayWithCapacity:numFrames];
		for (int i = 0; i < numFrames; ++i) {
			batch->requested[i] = CMTimeMakeWithSeconds(durationSec * (i + 1) / (numFrames + 1), 600);
			[times addObject:[NSValue valueWithCMTime:batch->requested[i]]];
		}
		[gen generateCGImagesAsynchronouslyForTimes:times
		                           completionHandler:^(CMTime request, CGImageRef image, CMTime actual,
		                                               AVAssetImageGeneratorResult result, NSError* error) {
			(void)error;
			std::lock_guard<std::mutex> lock(batch->mutex);
			for (int i = 0; i < numFrames; ++i) {
				if (batch->done[i] || CMTimeCompare(batch->requested[i], request) != 0) continue;
				batch->done[i] = true;
				if (result == AVAssetImageGeneratorSucceeded && image) {
					batch->images[i] = CGImageRetain(image);
					batch->actual[i] = actual;
				}
				++batch->completed;
				batch->changed.notify_one();
				break;
			}
		}];
		if (!batch->Wait(numFrames, deadline, cancel)) {
			[gen cancelAllCGImageGeneration];
			return false;
		}
		for (int i = 0; i < numFrames; ++i) {
			Image frame;
			if (ExtractRGBFromCGImage(batch->images[i], cellW, cellH, frame)) {
				frames.push_back(std::move(frame));
				double actual = CMTimeGetSeconds(batch->actual[i]);
				if (!std::isfinite(actual) || actual < 0.0) actual = CMTimeGetSeconds(batch->requested[i]);
				timecodes.push_back(VideoHelpers::FormatTimecode(actual));
			} else if (!frames.empty()) {
				frames.push_back(frames.back());
				timecodes.push_back(timecodes.back());
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
