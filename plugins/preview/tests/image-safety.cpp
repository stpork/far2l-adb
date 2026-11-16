#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include "../src/Image.h"
#include "../src/decoder/ImageDecoder.h"
#include "fallback.inc"

static bool fail_allocation = false;
void* operator new(std::size_t size)
{
    if (fail_allocation && size >= 4096) throw std::bad_alloc();
    if (void* data = std::malloc(size ? size : 1)) return data;
    throw std::bad_alloc();
}
void operator delete(void* data) noexcept { std::free(data); }

class AllocationDecoder : public ImageDecoder {
public:
    bool Decode(const std::string&, Image& out, ImageDecodeInfo&, int, const DecodeCancelFlag*) override
    { out.Resize(256, 128); return true; }
    bool CanHandle(const char*) const override { return true; }
    const char* Name() const override { return "AllocationDecoder"; }
};

static void CheckFailure()
{
    Image source(256, 128), destination(3, 2);
    std::fill_n(static_cast<unsigned char*>(destination.Data()), destination.Size(), 42);
    const auto original = destination;
    bool failed = false;
    fail_allocation = true;
    try { source.Rotate(destination, true); } catch (const std::bad_alloc&) { failed = true; }
    fail_allocation = false;
    assert(failed && destination.Width() == original.Width() && destination.Height() == original.Height());
    assert(destination.Size() == original.Size() && !std::memcmp(destination.Data(), original.Data(), original.Size()));
    failed = false;
    try { destination.Resize(INT_MAX, INT_MAX, 4); } catch (const std::length_error&) { failed = true; }
    assert(failed && destination.Size() == original.Size());
    FallbackImageDecoder decoder({std::make_shared<AllocationDecoder>()});
    ImageDecodeInfo info;
    fail_allocation = true;
    const bool decoded = decoder.Decode("file.png", destination, info, 0, nullptr);
    fail_allocation = false;
    assert(!decoded && destination.Size() == original.Size());
    assert(decoder.Decode("file.png", destination, info, 0, nullptr));
    assert(destination.Width() == 256 && destination.Height() == 128);
}

static void CheckTransforms()
{
    for (const int channels : {3, 4}) for (const int width : {0, 1, 2, 17, 1025})
        for (const int height : {0, 1, 2, 3, 33}) {
            Image image(width, height, channels);
            for (size_t i = 0; i < image.Size(); ++i) static_cast<unsigned char*>(image.Data())[i] = (i * 31) % 251;
            const Image original = image;
            image.MirrorV();
            for (int y = 0; y < image.Height(); ++y) for (int x = 0; x < image.Width(); ++x)
                for (int c = 0; c < channels; ++c)
                    assert(*image.Ptr(x, y, c) == *original.Ptr(x, height - 1 - y, c));
            image.MirrorV();
            assert(image.Size() == original.Size());
            if (image.Size()) assert(!std::memcmp(image.Data(), original.Data(), image.Size()));
            if (!image.Size()) continue;
            Image rotated, restored;
            image.Rotate(rotated, true);rotated.Rotate(restored, false);
            assert(restored.Width() == width && restored.Height() == height);
            assert(!std::memcmp(restored.Data(), original.Data(), original.Size()));
        }
    Image source(256, 128, 3), scaled, rotated;
    for (bool native : {false, true}) {
        source.Scale(scaled, .5, native);
        assert(scaled.Width() == 128 && scaled.Height() == 64);
        source.Scale(scaled, 2, native);
        assert(scaled.Width() == 512 && scaled.Height() == 256);
        source.RotateArbitrary(rotated, 7, false, native);
        assert(rotated.Width() >= source.Width() && rotated.Height() >= source.Height());
    }
}

void CreateCrossPlatformDecoders(std::vector<std::shared_ptr<ImageDecoder>>&);
#ifdef __APPLE__
void CreateMacDecoders(std::vector<std::shared_ptr<ImageDecoder>>&);
#endif

int main(int argc, char** argv)
{
    CheckFailure();
    CheckTransforms();
    std::vector<std::shared_ptr<ImageDecoder>> backends;
    CreateCrossPlatformDecoders(backends);
#ifdef __APPLE__
    CreateMacDecoders(backends);
#endif
    for (const auto& backend : backends) if (backend->CanHandle("png")) {
        Image image;
        ImageDecodeInfo info;
        assert(backend->Decode(argv[1], image, info, 0, nullptr));
        assert(image.Width() == 4 && image.Height() == 2 && info.sourceWidth == 4 && info.sourceHeight == 2);
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 4; ++x) for (int c = 0; c < 3; ++c)
            assert(*image.Ptr(x, y, c) == x * 3 + c);
        FallbackImageDecoder wrapped({backend});
        fail_allocation = true;
        const bool decoded = wrapped.Decode(argv[2], image, info, 0, nullptr);
        fail_allocation = false;
        assert(!decoded && image.Width() == 4 && image.Height() == 2);
        assert(wrapped.Decode(argv[2], image, info, 0, nullptr));
        assert(image.Width() == 256 && image.Height() == 128);
    }
}
