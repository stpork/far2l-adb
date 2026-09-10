#include "VideoDecoder.h"
#include "external/stb_image.h"
#include "../PreviewLog.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <array>
#include <memory>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <cerrno>

extern char **environ;

#ifdef HAVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}
#endif

namespace {

class CrossPlatformVideoDecoder final : public ImageDecoder {
public:
	bool Decode(const std::string& path, Image& out, ImageDecodeInfo& info,
	            int maxPixelSize, const DecodeCancelFlag* cancel) override;
	bool CanHandle(const char* ext) const override;
	const char* Name() const override { return "CrossPlatformVideo"; }

private:
#ifdef HAVE_FFMPEG
	bool DecodeViaLibAV(const std::string& path, Image& out, ImageDecodeInfo& info,
	                    int maxPixelSize, const DecodeCancelFlag* cancel);
#endif
	bool DecodeViaFFmpegCLI(const std::string& path, Image& out, ImageDecodeInfo& info,
	                        int maxPixelSize, const DecodeCancelFlag* cancel);
};

bool CrossPlatformVideoDecoder::CanHandle(const char* ext) const
{
	return VideoHelpers::IsVideoExtension(ext);
}

#ifdef HAVE_FFMPEG
bool CrossPlatformVideoDecoder::DecodeViaLibAV(const std::string& path, Image& out, ImageDecodeInfo& info,
                                              int maxPixelSize, const DecodeCancelFlag* cancel)
{
	AVFormatContext* fmtCtx = nullptr;
	if (avformat_open_input(&fmtCtx, path.c_str(), nullptr, nullptr) != 0) {
		return false;
	}
	struct FormatGuard {
		AVFormatContext* ctx;
		~FormatGuard() { if (ctx) avformat_close_input(&ctx); }
	} fmtGuard{fmtCtx};

	if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
		return false;
	}

	int videoStreamIdx = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	if (videoStreamIdx < 0) {
		return false;
	}

	AVCodecParameters* codecPar = fmtCtx->streams[videoStreamIdx]->codecpar;
	const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
	if (!codec) return false;

	AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
	if (!codecCtx) return false;
	struct CodecGuard {
		AVCodecContext* ctx;
		~CodecGuard() { if (ctx) avcodec_free_context(&ctx); }
	} codecGuard{codecCtx};

	if (avcodec_parameters_to_context(codecCtx, codecPar) < 0 ||
	    avcodec_open2(codecCtx, codec, nullptr) < 0) {
		return false;
	}

	double durationSec = (fmtCtx->duration > 0) ? (static_cast<double>(fmtCtx->duration) / AV_TIME_BASE) : 10.0;
	if (durationSec <= 0.0 || std::isnan(durationSec) || std::isinf(durationSec)) durationSec = 10.0;

	int videoW = codecCtx->width > 0 ? codecCtx->width : 1920;
	int videoH = codecCtx->height > 0 ? codecCtx->height : 1080;

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

	SwsContext* sws = sws_getContext(codecCtx->width, codecCtx->height, codecCtx->pix_fmt,
	                                 cellW, cellH, AV_PIX_FMT_RGB24,
	                                 SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
	if (!sws) return false;
	struct SwsGuard {
		SwsContext* s;
		~SwsGuard() { if (s) sws_freeContext(s); }
	} swsGuard{sws};

	AVFrame* rawFrame = av_frame_alloc();
	AVPacket* packet = av_packet_alloc();
	if (!rawFrame || !packet) {
		if (rawFrame) av_frame_free(&rawFrame);
		if (packet) av_packet_free(&packet);
		return false;
	}
	struct FrameGuard {
		AVFrame* f;
		AVPacket* p;
		~FrameGuard() {
			if (f) av_frame_free(&f);
			if (p) av_packet_free(&p);
		}
	} frameGuard{rawFrame, packet};

	const int numFrames = 9;
	std::vector<Image> frames;
	std::vector<std::string> timecodes;
	frames.reserve(numFrames);
	timecodes.reserve(numFrames);

	for (int i = 0; i < numFrames; ++i) {
		if (DecodeCancelled(cancel)) return false;

		double targetSec = durationSec * (static_cast<double>(i + 1) / (numFrames + 1));
		int64_t targetTs = static_cast<int64_t>(targetSec * AV_TIME_BASE);
		av_seek_frame(fmtCtx, -1, targetTs, AVSEEK_FLAG_BACKWARD);
		avcodec_flush_buffers(codecCtx);

		bool gotFrame = false;
		while (!gotFrame && av_read_frame(fmtCtx, packet) >= 0) {
			if (DecodeCancelled(cancel)) return false;

			if (packet->stream_index == videoStreamIdx) {
				if (avcodec_send_packet(codecCtx, packet) >= 0) {
					if (avcodec_receive_frame(codecCtx, rawFrame) >= 0) {
						Image frame(cellW, cellH, 3);
						uint8_t* dstData[1] = { static_cast<uint8_t*>(frame.Data()) };
						int dstLinesize[1] = { cellW * 3 };

						sws_scale(sws, rawFrame->data, rawFrame->linesize,
						          0, codecCtx->height, dstData, dstLinesize);

						frames.push_back(std::move(frame));
						timecodes.push_back(VideoHelpers::FormatTimecode(targetSec));
						gotFrame = true;
					}
				}
			}
			av_packet_unref(packet);
		}

		if (!gotFrame && !frames.empty()) {
			frames.push_back(frames.back());
			timecodes.push_back(timecodes.back());
		}
	}

	if (frames.empty()) return false;

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
#endif

bool CrossPlatformVideoDecoder::DecodeViaFFmpegCLI(const std::string& path, Image& out, ImageDecodeInfo& info,
                                                   int maxPixelSize, const DecodeCancelFlag* cancel)
{
	int pipefd[2];
	if (pipe(pipefd) != 0) return false;

	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_addclose(&actions, pipefd[0]);
	posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
	posix_spawn_file_actions_addclose(&actions, pipefd[1]);
	posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
	posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);

	const char* filter = "thumbnail=30,scale='if(gt(iw,ih),360,-2)':'if(gt(iw,ih),-2,360)',tile=3x3";
	const char* argv[] = {
		"ffmpeg",
		"-v", "error",
		"-ss", "0.5",
		"-i", path.c_str(),
		"-vf", filter,
		"-frames:v", "1",
		"-f", "image2pipe",
		"-vcodec", "png",
		"-",
		nullptr
	};

	pid_t pid = 0;
	int spawn_res = posix_spawnp(&pid, "ffmpeg", &actions, nullptr, const_cast<char* const*>(argv), environ);
	posix_spawn_file_actions_destroy(&actions);
	close(pipefd[1]);

	if (spawn_res != 0) {
		close(pipefd[0]);
		return false;
	}

	std::vector<uint8_t> buffer;
	buffer.reserve(256 * 1024);
	std::array<uint8_t, 65536> chunk;
	bool cancelled = false;

	while (true) {
		if (DecodeCancelled(cancel)) {
			cancelled = true;
			break;
		}
		ssize_t bytesRead = read(pipefd[0], chunk.data(), chunk.size());
		if (bytesRead > 0) {
			buffer.insert(buffer.end(), chunk.data(), chunk.data() + bytesRead);
		} else if (bytesRead == 0) {
			break; // EOF
		} else {
			if (errno == EINTR) continue;
			break;
		}
	}
	close(pipefd[0]);

	if (cancelled) {
		kill(pid, SIGKILL);
		waitpid(pid, nullptr, 0);
		return false;
	}

	int status = 0;
	waitpid(pid, &status, 0);

	if (buffer.empty()) return false;

	int w = 0, h = 0, comp = 0;
	stbi_uc* rgb = stbi_load_from_memory(buffer.data(), static_cast<int>(buffer.size()), &w, &h, &comp, 3);
	if (!rgb || w <= 0 || h <= 0) {
		if (rgb) stbi_image_free(rgb);
		return false;
	}

	out.Resize(w, h, 3);
	out.Assign(rgb, static_cast<size_t>(w) * h * 3);
	stbi_image_free(rgb);

	info.sourceWidth = w;
	info.sourceHeight = h;
	info.orientation = 1;
	info.fullResolution = true;
	return true;
}

bool CrossPlatformVideoDecoder::Decode(const std::string& path, Image& out, ImageDecodeInfo& info,
                                       int maxPixelSize, const DecodeCancelFlag* cancel)
{
#ifdef HAVE_FFMPEG
	if (DecodeViaLibAV(path, out, info, maxPixelSize, cancel)) {
		return true;
	}
#endif
	return DecodeViaFFmpegCLI(path, out, info, maxPixelSize, cancel);
}

} // namespace

void CreateCrossPlatformVideoDecoders(std::vector<std::shared_ptr<ImageDecoder>>& decoders)
{
	decoders.push_back(std::make_shared<CrossPlatformVideoDecoder>());
}
