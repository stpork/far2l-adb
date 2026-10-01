// Standalone runtime-codec regression. No link dependency on libheif or libav*.
#include <cassert>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <dlfcn.h>
#include <spawn.h>
#include <libheif/heif.h>

static bool hideHeif = false;
static bool incompleteHeif = false;
static bool hideFFmpeg = false;
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
    return hideFFmpeg ? ENOENT : posix_spawn(pid, path, actions, attrs, argv, env);
}
static int TestSpawnp(pid_t* pid, const char* path, const posix_spawn_file_actions_t* actions,
                      const posix_spawnattr_t* attrs, char* const argv[], char* const env[])
{
    ++ffmpegAttempts;
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
