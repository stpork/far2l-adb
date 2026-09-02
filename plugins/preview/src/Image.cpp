#include <cmath>
#include <vector>
#include <algorithm>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "Image.h"
#include "PreviewLog.h"
#include "decoder/external/stb_image_resize2.h"

#ifdef __APPLE__
#include <Accelerate/Accelerate.h>
#endif

Image::Image(int width, int height, unsigned char bytes_per_pixel)
{
	Resize(width, height, bytes_per_pixel);
}

void Image::MirrorH()
{
	for (int y = 0; y < _height; ++y) {
		for (int i = 0; i < _width - 1 - i; ++i) {
			for (unsigned char ch = 0; ch < _bytes_per_pixel; ++ch) {
				std::swap(*Ptr(i, y, ch), *Ptr(_width - 1 - i, y, ch));
			}
		}
	}
}

void Image::MirrorV()
{
	for (int x = 0; x < _width; ++x) {
		for (int i = 0; i < _height - 1 - i; ++i) {
			for (unsigned char ch = 0; ch < _bytes_per_pixel; ++ch) {
				std::swap(*Ptr(x, i, ch), *Ptr(x, _height - 1 - i, ch));
			}
		}
	}
}

void Image::Swap(Image &another)
{
	_data.swap(another._data);
	std::swap(_width, another._width);
	std::swap(_height, another._height);
	std::swap(_bytes_per_pixel, another._bytes_per_pixel);
}

void Image::Assign(const void *data, size_t size)
{
	assert(size <= _data.size() && "Image::Assign: source larger than allocated buffer");
	memcpy(_data.data(), data, size);
}

void Image::Resize(int width, int height, unsigned char bytes_per_pixel)
{
	assert(bytes_per_pixel == 3 || bytes_per_pixel == 4);

	if (width < 0 || height < 0) {
		_data.clear(); _width = 0; _height = 0;
		return;
	}

	const size_t bytes_size = size_t(width) * size_t(height) * size_t(bytes_per_pixel);
	if (height && bytes_size < size_t(width)) { // overflow
		_data.clear(); _width = 0; _height = 0;
		return;
	}

	_data.resize(bytes_size);
	_width = width;
	_height = height;
	_bytes_per_pixel = bytes_per_pixel;
}

void Image::Rotate(Image &dst, bool clockwise) const
{
	dst.Resize(_height, _width, _bytes_per_pixel);
	for (int y = 0; y < _height; ++y) {
		for (int x = 0; x < _width; ++x) {
			auto *dpix = dst.Ptr(clockwise ? _height - 1 - y : y, x);
			const auto *spix = Ptr(clockwise ? x : _width - 1 - x, y);
			for (unsigned char ch = 0; ch < _bytes_per_pixel; ++ch) {
				dpix[ch] = spix[ch];
			}
		}
	}
}

void Image::Blit(Image &dst, int dst_left, int dst_top, int width, int height, int src_left, int src_top) const
{
	assert(_bytes_per_pixel == dst._bytes_per_pixel);
	if (dst_left < 0 || src_left < 0) {
		const int most_negative_left = std::min(src_left, dst_left);
		width+= most_negative_left;
		src_left-= most_negative_left;
		dst_left-= most_negative_left;
	}
	if (dst_top < 0 || src_top < 0) {
		const int most_negative_top = std::min(src_top, dst_top);
		height+= most_negative_top;
		src_top-= most_negative_top;
		dst_top-= most_negative_top;
	}
	if (width <= 0 || height <= 0) {
		return;
	}
	if (dst_left + width > dst._width) {
		width = dst._width - dst_left;
	}
	if (src_left + width > _width) {
		width = _width - src_left;
	}
	if (dst_top + height > dst._height) {
		height = dst._height - dst_top;
	}
	if (src_top + height > _height) {
		height = _height - src_top;
	}
	const int cpy_width = width * _bytes_per_pixel;
	if (width <= 0 || cpy_width < width) { // || overflow guard
		return;
	}

	for (int y = 0; y < height; ++y) {
		memcpy(dst.Ptr(dst_left, dst_top + y), Ptr(src_left, src_top + y), cpy_width);
	}
}

void Image::Scale(Image &dst, double scale) const
{
	if (fabs(scale - 1.0) < 0.0001) {
		dst = *this;
		return;
	}

	const int out_w = std::max(1, int(scale * _width));
	const int out_h = std::max(1, int(scale * _height));
	dst.Resize(out_w, out_h, _bytes_per_pixel);
	if (_data.empty() || dst._data.empty()) {
		std::fill(dst._data.begin(), dst._data.end(), 0);
		return;
	}

#ifdef __APPLE__
	// Convert RGB→ARGB, scale with vImage (Accelerate framework, SIMD), convert ARGB→RGB
	if (_bytes_per_pixel == 3) {
		const size_t src_argb_bytes = (size_t)_width * _height * 4;
		const size_t dst_argb_bytes = (size_t)out_w * out_h * 4;
		if (_scratch_argb_src.size() < src_argb_bytes) _scratch_argb_src.resize(src_argb_bytes);
		if (_scratch_argb_dst.size() < dst_argb_bytes) _scratch_argb_dst.resize(dst_argb_bytes);

		const uint8_t *s = _data.data();
		uint8_t *a = _scratch_argb_src.data();
		for (int i = 0, n = _width * _height; i < n; ++i) {
			a[i*4+0] = 255;
			a[i*4+1] = s[i*3+0];
			a[i*4+2] = s[i*3+1];
			a[i*4+3] = s[i*3+2];
		}

		vImage_Buffer src_buf = { _scratch_argb_src.data(), (vImagePixelCount)_height, (vImagePixelCount)_width, (size_t)_width * 4 };
		vImage_Buffer dst_buf = { _scratch_argb_dst.data(), (vImagePixelCount)out_h, (vImagePixelCount)out_w, (size_t)out_w * 4 };

		if (vImageScale_ARGB8888(&src_buf, &dst_buf, nullptr, kvImageHighQualityResampling) == kvImageNoError) {
			const uint8_t *d = _scratch_argb_dst.data();
			uint8_t *out = dst._data.data();
			for (int i = 0, n = out_w * out_h; i < n; ++i) {
				out[i*3+0] = d[i*4+1];
				out[i*3+1] = d[i*4+2];
				out[i*3+2] = d[i*4+3];
			}
			return;
		}
		// Fall through to stbir on error
	} else {
		// 4-channel: vImage works directly (channel labels don't matter for pure scaling)
		vImage_Buffer src_buf = { const_cast<void*>(static_cast<const void*>(_data.data())), (vImagePixelCount)_height, (vImagePixelCount)_width, (size_t)_width * 4 };
		vImage_Buffer dst_buf = { dst._data.data(), (vImagePixelCount)out_h, (vImagePixelCount)out_w, (size_t)out_w * 4 };
		if (vImageScale_ARGB8888(&src_buf, &dst_buf, nullptr, kvImageHighQualityResampling) == kvImageNoError) {
			return;
		}
	}
#endif

	// Cross-platform: stb_image_resize2 (SSE2/NEON SIMD, compiled in ImageDecoder_stb.cpp)
	const stbir_pixel_layout layout = (_bytes_per_pixel == 4) ? STBIR_RGBA : STBIR_RGB;
	stbir_resize_uint8_linear(
		_data.data(), _width, _height, 0,
		dst._data.data(), out_w, out_h, 0,
		layout);
}


void Image::RotateArbitrary(Image &dst, double angle_degrees, bool high_quality) const
{
	if (_data.empty()) {
		dst.Resize(0, 0, _bytes_per_pixel);
		return;
	}

	// Normalize angle to -180..180
	while (angle_degrees > 180) angle_degrees -= 360;
	while (angle_degrees < -180) angle_degrees += 360;

	// For 90-degree multiples, use fast path
	if (fabs(angle_degrees) < 0.1) {
		dst = *this;
		return;
	}
	if (fabs(fabs(angle_degrees) - 90) < 0.1) {
		Rotate(dst, angle_degrees > 0);
		return;
	}
	if (fabs(fabs(angle_degrees) - 180) < 0.1) {
		dst = *this;
		dst.MirrorH();
		dst.MirrorV();
		return;
	}

#ifdef __APPLE__
	// Use vImage for arbitrary rotation on macOS
	const double angle_rad = -angle_degrees * M_PI / 180.0;  // Negative for clockwise convention

	// Calculate new dimensions for rotated image
	const double cos_a = fabs(cos(angle_rad));
	const double sin_a = fabs(sin(angle_rad));
	const int new_width = int(_width * cos_a + _height * sin_a + 0.5);
	const int new_height = int(_width * sin_a + _height * cos_a + 0.5);

	const size_t src_stride = (size_t)_width * 4;
	const size_t dst_stride = (size_t)new_width * 4;
	const size_t src_size = src_stride * _height;
	const size_t dst_size = dst_stride * new_height;

	// Grow scratch buffers only if needed (avoid reallocations in hot path)
	if (_scratch_argb_src.size() < src_size) {
		_scratch_argb_src.resize(src_size);
	}
	if (_scratch_argb_dst.size() < dst_size) {
		_scratch_argb_dst.resize(dst_size);
	}

	// RGB → ARGB (manual, vImageConvert requires planar input)
	uint8_t *argb_src = _scratch_argb_src.data();
	for (int y = 0; y < _height; ++y) {
		const uint8_t *src_row = Ptr(0, y);
		uint8_t *dst_row = argb_src + y * src_stride;
		for (int x = 0; x < _width; ++x) {
			dst_row[x * 4 + 0] = 255;                  // A
			dst_row[x * 4 + 1] = src_row[x * 3 + 0];   // R
			dst_row[x * 4 + 2] = src_row[x * 3 + 1];   // G
			dst_row[x * 4 + 3] = src_row[x * 3 + 2];   // B
		}
	}

	// Clear destination buffer (black background)
	memset(_scratch_argb_dst.data(), 0, dst_size);

	vImage_Buffer src_buf = {
		.data = argb_src,
		.height = (vImagePixelCount)_height,
		.width = (vImagePixelCount)_width,
		.rowBytes = src_stride
	};

	vImage_Buffer dst_buf = {
		.data = _scratch_argb_dst.data(),
		.height = (vImagePixelCount)new_height,
		.width = (vImagePixelCount)new_width,
		.rowBytes = dst_stride
	};

	// Background color (black, ARGB)
	Pixel_8888 bgColor = {255, 0, 0, 0};  // ARGB: Alpha=255, R=0, G=0, B=0

	// Perform rotation - use fast mode for interactive rotation
	vImage_Flags flags = kvImageBackgroundColorFill;
	if (high_quality) {
		flags |= kvImageHighQualityResampling;
	}

	vImage_Error err = vImageRotate_ARGB8888(
		&src_buf,
		&dst_buf,
		nullptr,  // temp buffer (auto-allocated)
		(float)angle_rad,
		bgColor,
		flags
	);

	if (err != kvImageNoError) {
		DBG("vImageRotate_ARGB8888 failed: %ld", (long)err);
		dst = *this;
		return;
	}

	// ARGB → RGB (manual)
	dst.Resize(new_width, new_height, 3);
	const uint8_t *argb_dst = _scratch_argb_dst.data();
	for (int y = 0; y < new_height; ++y) {
		const uint8_t *src_row = argb_dst + y * dst_stride;
		uint8_t *dst_row = dst.Ptr(0, y);
		for (int x = 0; x < new_width; ++x) {
			dst_row[x * 3 + 0] = src_row[x * 4 + 1];  // R
			dst_row[x * 3 + 1] = src_row[x * 4 + 2];  // G
			dst_row[x * 3 + 2] = src_row[x * 4 + 3];  // B
		}
	}
#else
	// High-performance software fallback for non-macOS platforms
	const double angle_rad = -angle_degrees * M_PI / 180.0;
	const double cos_a = cos(angle_rad);
	const double sin_a = sin(angle_rad);

	const int new_width = int(fabs(_width * cos_a) + fabs(_height * sin_a) + 0.5);
	const int new_height = int(fabs(_width * sin_a) + fabs(_height * cos_a) + 0.5);

	dst.Resize(new_width, new_height, _bytes_per_pixel);
	memset(dst.Data(), 0, dst.Size()); // Black background

	const double cx = _width / 2.0;
	const double cy = _height / 2.0;
	const double ncx = new_width / 2.0;
	const double ncy = new_height / 2.0;

	// Use 16.16 fixed-point arithmetic
	const int64_t fixed_one = 1LL << 16;
	const int64_t f_cos_a = (int64_t)(cos_a * fixed_one);
	const int64_t f_sin_a = (int64_t)(sin_a * fixed_one);
	const int64_t f_cx = (int64_t)(cx * fixed_one);
	const int64_t f_cy = (int64_t)(cy * fixed_one);
	const int64_t f_ncx = (int64_t)(ncx * fixed_one);
	const int64_t f_ncy = (int64_t)(ncy * fixed_one);

	auto rotate_y_range = [&](int y_begin, int y_end) {
		for (int y = y_begin; y < y_end; ++y) {
			int64_t fy_rel = (int64_t)y * fixed_one - f_ncy;
			
			// Pre-calculate terms that only depend on y
			int64_t base_sx = - (fy_rel * f_sin_a >> 16) + f_cx;
			int64_t base_sy = (fy_rel * f_cos_a >> 16) + f_cy;

			uint8_t *dst_row = dst.Ptr(0, y);

			for (int x = 0; x < new_width; ++x) {
				int64_t fx_rel = (int64_t)x * fixed_one - f_ncx;

				// sx = fx_rel * cos_a - fy_rel * sin_a + cx
				// sy = fx_rel * sin_a + fy_rel * cos_a + cy
				int64_t sx = (fx_rel * f_cos_a >> 16) + base_sx;
				int64_t sy = (fx_rel * f_sin_a >> 16) + base_sy;

				int src_x = (int)((sx + (fixed_one >> 1)) >> 16);
				int src_y = (int)((sy + (fixed_one >> 1)) >> 16);

				if (src_x >= 0 && src_x < _width && src_y >= 0 && src_y < _height) {
					const uint8_t *src_pix = Ptr(src_x, src_y);
					for (int c = 0; c < _bytes_per_pixel; ++c) {
						dst_row[x * _bytes_per_pixel + c] = src_pix[c];
					}
				}
			}
		}
	};

	const int hw_cpu_count = int(std::thread::hardware_concurrency());
	const int use_cpu_count = (hw_cpu_count > 0) ? std::min(16, hw_cpu_count) : 1;

	if (use_cpu_count > 1 && new_height > 16) {
		std::vector<std::thread> threads;
		int y_start = 0;
		int chunk = new_height / use_cpu_count;
		for (int i = 0; i < use_cpu_count - 1; ++i) {
			threads.emplace_back(rotate_y_range, y_start, y_start + chunk);
			y_start += chunk;
		}
		rotate_y_range(y_start, new_height);
		for (auto &t : threads) t.join();
	} else {
		rotate_y_range(0, new_height);
	}

	(void)high_quality; 
#endif
}

