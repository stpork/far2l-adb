#pragma once
#include "ImageDecoder.h"
#include <vector>
#include <memory>

// Factory function declaration for TIFF decoder
void CreateTiffDecoder(std::vector<std::shared_ptr<ImageDecoder>>& decoders);
