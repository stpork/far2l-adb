#include "VideoDecoder.h"
#include <algorithm>
#include <cstring>

namespace VideoHelpers {

// 5x7 bitmap font for digits '0'-'9', ':', '.', ' '
// Each byte represents a row of 5 bits (bits 0..4, LSB on right or MSB on left)
// We use 5 bits per row: bit 4 is leftmost, bit 0 is rightmost.
static const uint8_t kFont5x7[13][7] = {
	// 0: '0'
	{ 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E },
	// 1: '1'
	{ 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
	// 2: '2'
	{ 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },
	// 3: '3'
	{ 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E },
	// 4: '4'
	{ 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 },
	// 5: '5'
	{ 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },
	// 6: '6'
	{ 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E },
	// 7: '7'
	{ 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
	// 8: '8'
	{ 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E },
	// 9: '9'
	{ 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },
	// 10: ':'
	{ 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 },
	// 11: '.'
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C },
	// 12: ' '
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
};

static int GetGlyphIndex(char ch)
{
	if (ch >= '0' && ch <= '9') return ch - '0';
	if (ch == ':') return 10;
	if (ch == '.') return 11;
	return 12; // space or unknown
}

void StampTimecodeBadge(Image& img, int x, int y, const std::string& text)
{
	if (text.empty() || img.Width() <= 0 || img.Height() <= 0) return;

	const int scale = 2; // 2x magnification -> 10x14 px characters
	const int charW = 5 * scale;
	const int charH = 7 * scale;
	const int pitchX = charW + 2; // 12 px pitch
	const int textW = static_cast<int>(text.size()) * pitchX;
	const int textH = charH;

	const int padX = 5;
	const int padY = 3;
	const int boxX1 = std::max(0, x - padX);
	const int boxY1 = std::max(0, y - padY);
	const int boxX2 = std::min(img.Width() - 1, x + textW + padX);
	const int boxY2 = std::min(img.Height() - 1, y + textH + padY);

	// 1. Draw dark background box (semi-transparent dimming or dark slate)
	for (int by = boxY1; by <= boxY2; ++by) {
		for (int bx = boxX1; bx <= boxX2; ++bx) {
			unsigned char* p = img.Ptr(bx, by);
			// Blend towards dark translucent black (20% original + 80% black)
			p[0] = static_cast<unsigned char>((p[0] * 3) / 16);
			p[1] = static_cast<unsigned char>((p[1] * 3) / 16);
			p[2] = static_cast<unsigned char>((p[2] * 3) / 16);
		}
	}

	// 2. Draw text glyphs in high-contrast crisp white with 1px drop shadow
	int curX = x;
	for (char ch : text) {
		int gIdx = GetGlyphIndex(ch);
		const uint8_t* glyph = kFont5x7[gIdx];

		for (int row = 0; row < 7; ++row) {
			uint8_t rowBits = glyph[row];
			for (int col = 0; col < 5; ++col) {
				// Bit 4 is leftmost
				if (rowBits & (1 << (4 - col))) {
					for (int dy = 0; dy < scale; ++dy) {
						for (int dx = 0; dx < scale; ++dx) {
							int px = curX + col * scale + dx;
							int py = y + row * scale + dy;
							if (px >= 0 && px < img.Width() && py >= 0 && py < img.Height()) {
								unsigned char* p = img.Ptr(px, py);
								p[0] = 0xFF;
								p[1] = 0xFF;
								p[2] = 0xFF;
							}
						}
					}
				}
			}
		}
		curX += pitchX;
	}
}

bool ComposeGrid(const std::vector<Image>& frames,
                 const std::vector<std::string>& timecodes,
                 int cellWidth, int cellHeight,
                 int margin, Image& out)
{
	if (frames.empty() || cellWidth <= 0 || cellHeight <= 0) return false;

	const int cols = 3;
	const int rows = 3;
	const int totalW = cols * cellWidth + (cols + 1) * margin;
	const int totalH = rows * cellHeight + (rows + 1) * margin;

	out.Resize(totalW, totalH, 3);
	// Fill background with elegant dark charcoal color (0x16, 0x16, 0x18)
	for (int y = 0; y < totalH; ++y) {
		for (int x = 0; x < totalW; ++x) {
			unsigned char* p = out.Ptr(x, y);
			p[0] = 0x16;
			p[1] = 0x16;
			p[2] = 0x18;
		}
	}

	for (size_t i = 0; i < frames.size() && i < 9; ++i) {
		int r = static_cast<int>(i / cols);
		int c = static_cast<int>(i % cols);
		int cellX = margin + c * (cellWidth + margin);
		int cellY = margin + r * (cellHeight + margin);

		const Image& frame = frames[i];
		if (frame.Width() == cellWidth && frame.Height() == cellHeight) {
			frame.Blit(out, cellX, cellY, cellWidth, cellHeight, 0, 0);
		}

		if (i < timecodes.size() && !timecodes[i].empty()) {
			int badgeX = cellX + 8;
			int badgeY = cellY + cellHeight - 20;
			StampTimecodeBadge(out, badgeX, badgeY, timecodes[i]);
		}
	}

	return true;
}

} // namespace VideoHelpers
