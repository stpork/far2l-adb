#ifndef PREVIEWLOG_H
#define PREVIEWLOG_H

#include <stdio.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(DEBUG) || defined(_DEBUG)
void PreviewDebugLog(const char *format, ...);
#endif

#ifdef __cplusplus
}
#endif

// Debug macros - completely absent from release builds; enable with -DDEBUG.
// Release path uses a variadic template no-op so DBG arguments are still
// parsed and counted as "used" (no -Wunused / no [[maybe_unused]] needed);
// the empty body is DCE'd to nothing.
#if defined(DEBUG) || defined(_DEBUG)
#define DBG(fmt, ...) PreviewDebugLog("[%s] " fmt, __FUNCTION__, ##__VA_ARGS__)
#else
#ifdef __cplusplus
template<typename... Args>
inline void PreviewDbgNoOp(Args &&...) {}
#define DBG(fmt, ...) PreviewDbgNoOp(fmt, ##__VA_ARGS__)
#else
#define DBG(fmt, ...) ((void)0)
#endif
#endif

#endif // PREVIEWLOG_H
