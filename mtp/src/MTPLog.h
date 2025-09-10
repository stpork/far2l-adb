#ifndef MTPLOG_H
#define MTPLOG_H

#include <stdio.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void DebugLog(const char *format, ...);

#ifdef __cplusplus
}
#endif

// Debug macros
#define DBG(fmt, ...) DebugLog("[%s] " fmt, __FUNCTION__, ##__VA_ARGS__)
#define _DBG(fmt, ...) // Empty macro - removes debug lines when expanded

#endif // MTPLOG_H