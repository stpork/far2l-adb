#import <AppKit/AppKit.h>
#import "pasteboard.h"

// Clipboard backend calls are serialized on the wx main thread.
static NSPasteboardItem *pending;

static NSPasteboardItem *PendingItem()
{
	if (!pending) pending = [[NSPasteboardItem alloc] init];
	return pending;
}

static bool StageText(const void *bytes, size_t length, NSStringEncoding encoding)
{
	NSString *text = [[NSString alloc] initWithBytes:bytes length:length encoding:encoding];
	bool ok = text && [PendingItem() setString:text forType:NSPasteboardTypeString];
#if !__has_feature(objc_arc)
	[text release];
#endif
	return ok;
}

bool StagePasteboardText(const char *text, size_t count)
{
	return StageText(text, count, NSUTF8StringEncoding);
}

bool StagePasteboardText(const wchar_t *text, size_t count)
{
	return StageText(text, count * sizeof(*text), NSUTF32LittleEndianStringEncoding);
}

bool StagePasteboardData(CFStringRef type, const void *data, size_t size)
{
	NSData *bytes = [NSData dataWithBytes:data length:size];
	return type && [PendingItem() setData:bytes forType:(__bridge NSString *)type];
}

void DiscardPasteboardData()
{
#if !__has_feature(objc_arc)
	[pending release];
#endif
	pending = nil;
}

static bool PublishTo(NSPasteboard *board)
{
	if (!pending) return true;
	[board clearContents];
	bool ok = [board writeObjects:@[pending]];
	DiscardPasteboardData();
	return ok;
}

bool PublishPasteboardData()
{
	return PublishTo([NSPasteboard generalPasteboard]);
}
