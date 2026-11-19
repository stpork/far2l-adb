// Standalone runtime-codec regression. No link dependency on libheif or libav*.
#include <cassert>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <dlfcn.h>
#include <spawn.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <libheif/heif.h>

static bool hideHeif = false;
static bool incompleteHeif = false;
static bool hideFFmpeg = false;
static int blockedFFmpeg = 0;
static std::atomic<pid_t> blockedPid{0};
static int openLibraries = 0;
static int heifAttempts = 0;
static int ffmpegAttempts = 0;
static void* TestDlopen(const char* path, int flags)
{
    ++heifAttempts;
    if (hideHeif) return nullptr;
    void* handle = dlopen(path, flags);
    if (handle) ++openLibraries;
    return handle;
}
static void* TestDlsym(void* handle, const char* name)
{
    if (incompleteHeif && !strcmp(name, "heif_decode_image")) return nullptr;
    return dlsym(handle, name);
}
static int TestDlclose(void* handle)
{
    --openLibraries;
    return dlclose(handle);
}
static int TestSpawn(pid_t* pid, const char* path, const posix_spawn_file_actions_t* actions,
                     const posix_spawnattr_t* attrs, char* const argv[], char* const env[])
{
    ++ffmpegAttempts;
    if (blockedFFmpeg) {
        char* sleepArgs[] = {const_cast<char*>("/bin/sleep"), const_cast<char*>("30"), nullptr};
        char* shellArgs[] = {const_cast<char*>("/bin/sh"), const_cast<char*>("-c"),
                            const_cast<char*>("exec 1>&-; exec /bin/sleep 30"), nullptr};
        char* oversizedArgs[] = {const_cast<char*>("/bin/dd"), const_cast<char*>("if=/dev/zero"),
                                 const_cast<char*>("bs=65536"), nullptr};
        const char* program = blockedFFmpeg == 1 ? "/bin/sleep"
                            : blockedFFmpeg == 2 ? "/bin/sh" : "/bin/dd";
        auto childArgs = blockedFFmpeg == 1 ? sleepArgs : blockedFFmpeg == 2 ? shellArgs : oversizedArgs;
        int result = posix_spawn(pid, program, actions, attrs, childArgs, env);
        if (result == 0) blockedPid.store(*pid);
        return result;
    }
    return hideFFmpeg ? ENOENT : posix_spawn(pid, path, actions, attrs, argv, env);
}
static int TestSpawnp(pid_t* pid, const char* path, const posix_spawn_file_actions_t* actions,
                      const posix_spawnattr_t* attrs, char* const argv[], char* const env[])
{
    ++ffmpegAttempts;
    if (blockedFFmpeg) return TestSpawn(pid, path, actions, attrs, argv, env);
    return hideFFmpeg ? ENOENT : posix_spawnp(pid, path, actions, attrs, argv, env);
}
#define dlopen TestDlopen
#define dlsym TestDlsym
#define dlclose TestDlclose
#define HAVE_HEIF 1
#include "../src/decoder/ImageDecoder_heif.cpp"
#undef dlopen
#undef dlsym
#undef dlclose
#define posix_spawn TestSpawn
#define posix_spawnp TestSpawnp
#include "../src/decoder/VideoDecoder_cross.cpp"
#undef posix_spawn
#undef posix_spawnp

#include "../src/decoder/ImageDecoder.cpp"
#include "../src/decoder/ImageDecoder_tiff.cpp"
#include "../src/decoder/ImageDecoder_webp.cpp"
// Avoid far2l configuration/UI dependencies in this standalone decoder test.
Settings::Settings() = default;
Settings g_settings;

int main(int argc, char** argv)
{
    assert(argc == 4); // PNG, HEIC/AVIF, video fixtures
    Image image;
    ImageDecodeInfo info;
    HeifImageDecoder heif;
    assert(heif.CanHandle("avif"));
    hideHeif = true;
    assert(!heif.Decode(argv[2], image, info, 64, nullptr));
    assert(openLibraries == 0);
    std::vector<std::shared_ptr<ImageDecoder>> stb;
    extern void CreateCrossPlatformDecoders(std::vector<std::shared_ptr<ImageDecoder>>&);
    CreateCrossPlatformDecoders(stb);
    assert(stb.front()->Decode(argv[1], image, info, 64));
    hideHeif = false;
    incompleteHeif = true;
    assert(!heif.Decode(argv[2], image, info, 64, nullptr));
    assert(openLibraries == 0);
    incompleteHeif = false;
    // Same decoder recovers after the library becomes available.
    assert(heif.Decode(argv[2], image, info, 64, nullptr));
    assert(image.Width() > 0 && image.Width() <= 64);
    assert(image.Height() > 0 && image.Height() <= 64);
    assert(openLibraries == 0);
    DecodeCancelFlag cancel{true};
    assert(!heif.Decode(argv[2], image, info, 64, &cancel));
    assert(openLibraries == 0);

    CrossPlatformVideoDecoder video;
    hideFFmpeg = true;
    assert(!video.Decode(argv[3], image, info, 64, nullptr));
    hideFFmpeg = false;
#ifdef __APPLE__
    // Exercise the Finder/Homebrew fallback rather than relying on shell PATH.
    setenv("PATH", "/usr/bin:/bin", 1);
#endif
    assert(video.Decode(argv[3], image, info, 64, nullptr));
    assert(image.Width() > 0 && image.Height() > 0);
    for (int mode : {1, 2}) {
        blockedFFmpeg = mode;
        blockedPid.store(0);
        DecodeCancelFlag stop{false};
        std::atomic<bool> finished{false};
        bool decoded = true;
        std::thread worker([&] {
            Image pending;
            ImageDecodeInfo pendingInfo;
            decoded = video.Decode(argv[3], pending, pendingInfo, 64, &stop);
            finished.store(true);
        });
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!blockedPid.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        stop.store(true);
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (!finished.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        bool promptCancellation = finished.load();
        pid_t child = blockedPid.load();
        if (!promptCancellation && child > 0) kill(child, SIGKILL);
        worker.join();
        assert(child > 0 && promptCancellation && !decoded);
        assert(waitpid(child, nullptr, WNOHANG) == -1 && errno == ECHILD);
    }
    puts("FFmpeg cancellation before output and after stdout closes: passed");
    blockedFFmpeg = 3;
    const auto started = std::chrono::steady_clock::now();
    const auto previousWidth = image.Width();
    assert(!video.Decode(argv[3], image, info, 64, nullptr));
    assert(std::chrono::steady_clock::now() - started < std::chrono::seconds(5));
    assert(image.Width() == previousWidth);
    assert(waitpid(blockedPid.load(), nullptr, WNOHANG) == -1 && errno == ECHILD);
    blockedFFmpeg = 0;
    puts("FFmpeg output budget and recovery: passed");
#if PREVIEW_HAS_NATIVE
    hideHeif = hideFFmpeg = true;
    heifAttempts = ffmpegAttempts = 0;
    auto factory = DecoderFactory::CreateDecoders();
    assert(factory.front()->Decode(argv[2], image, info, 64));
    assert(factory.front()->Decode(argv[3], image, info, 64));
    if (g_settings.NativeImplementation()) {
        assert(heifAttempts == 0 && ffmpegAttempts == 0);
    } else {
        assert(heifAttempts > 0 && ffmpegAttempts > 0);
    }
    assert(openLibraries == 0);
    puts("Native/cross-platform priority and native fallback passed");
#endif
    puts("Optional codecs: missing library/tool, missing symbol, retry, PNG and video decode passed");
}
