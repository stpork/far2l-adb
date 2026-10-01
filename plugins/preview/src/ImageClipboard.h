#pragma once

class Image;

// Publishes pixels, not a filename, to the macOS system clipboard.
bool CopyImageToClipboard(const Image &image);
