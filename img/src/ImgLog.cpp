#include "ImgLog.h"
#include <ctime>
#include <cstdio>
#include <cstdarg>
#include <utils.h>

void ImgDebugLog(const char* format, ...) {
    // Timestamp
    time_t now = time(nullptr);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char ts[24];
    strftime(ts, sizeof(ts), "%H:%M:%S", &tm_info);

    // Format message
    char buf[1024];
    va_list ap;
    va_start(ap, format);
    vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    std::string logPath = InMyConfig("plugins/img/debug.log");
    FILE *f = fopen(logPath.c_str(), "a");
    if (f) {
        fprintf(f, "[%s][img] %s\n", ts, buf);
        fclose(f);
    }
}
