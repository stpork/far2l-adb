#pragma once

#include "ImageDecoder.h"
#include <string>
#include <vector>
#include <cstdint>
#include <strings.h>

namespace VideoHelpers {

// Check if file extension corresponds to a video format
inline bool IsVideoExtension(const char* ext)
{
	if (!ext || !*ext) return false;

	static const char* const s_video_exts[] = {
		"mp4", "m4v", "mov", "mkv", "avi", "webm",
		"flv", "wmv", "ts", "mts", "m2ts", "vob",
		"3gp", "ogv", "mpg", "mpeg", "asf", "rm", "rmvb",
		nullptr
	};

	for (const char* const* p = s_video_exts; *p; ++p) {
		if (strcasecmp(ext, *p) == 0) {
			return true;
		}
	}
	return false;
}

// Format seconds into HH:MM:SS or MM:SS string
inline std::string FormatTimecode(double seconds)
{
	if (seconds < 0.0) seconds = 0.0;
	uint64_t total = static_cast<uint64_t>(seconds + 0.5);
	uint32_t s = total % 60;
	uint32_t m = (total / 60) % 60;
	uint32_t h = static_cast<uint32_t>(total / 3600);

	char buf[32];
	if (h > 0) {
		snprintf(buf, sizeof(buf), "%02u:%02u:%02u", h, m, s);
	} else {
		snprintf(buf, sizeof(buf), "%02u:%02u", m, s);
	}
	return std::string(buf);
}

// Draw a compact, clean timecode badge in an RGB image
void StampTimecodeBadge(Image& img, int x, int y, const std::string& text);

// Compose a 3x3 grid image from cell images
bool ComposeGrid(const std::vector<Image>& frames,
                 const std::vector<std::string>& timecodes,
                 int cellWidth, int cellHeight,
                 int margin, Image& out);

} // namespace VideoHelpers

#if PREVIEW_HAS_NATIVE
void CreateMacVideoDecoders(std::vector<std::shared_ptr<ImageDecoder>>& decoders);
#endif
void CreateCrossPlatformVideoDecoders(std::vector<std::shared_ptr<ImageDecoder>>& decoders);
