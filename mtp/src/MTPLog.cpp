#include "MTPLog.h"
#include <string.h>
#include <time.h>

void DebugLog(const char *format, ...)
{
    static FILE *logFile = nullptr;
    
    if (!logFile) {
        logFile = fopen("/tmp/mtp_plugin_debug.log", "a");
        if (!logFile) {
            // If we can't open the file, just return silently
            return;
        }
    }
    
    if (logFile) {
        // Get current time
        time_t now = time(nullptr);
        struct tm *tm_info = localtime(&now);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_info);
        
        // Write timestamp and check if it succeeded
        if (fprintf(logFile, "[%s] ", timestamp) < 0) {
            // File was deleted or has an error
            fclose(logFile);
            logFile = nullptr;
            return;
        }
        
        // Write the actual log message and check if it succeeded
        va_list args;
        va_start(args, format);
        if (vfprintf(logFile, format, args) < 0) {
            // File was deleted or has an error
            va_end(args);
            fclose(logFile);
            logFile = nullptr;
            return;
        }
        va_end(args);
        
        // Add newline to make log more readable
        if (fprintf(logFile, "\n") < 0) {
            // File was deleted or has an error
            fclose(logFile);
            logFile = nullptr;
            return;
        }
        
        // Ensure it's written to disk
        fflush(logFile);
    }
}