#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <cstddef>

bool StagePasteboardText(const char *text, size_t count);
bool StagePasteboardText(const wchar_t *text, size_t count);
bool StagePasteboardData(CFStringRef type, const void *data, size_t size);
void DiscardPasteboardData();
bool PublishPasteboardData();
