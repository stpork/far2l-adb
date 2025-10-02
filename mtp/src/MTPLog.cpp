#include "MTPLog.h"
#include <time.h>

void DebugLog(const char *format, ...)
{
    static FILE *logFile = nullptr;
    static const char* logPath = "/tmp/mtp_plugin_debug.log";
    
    // Ensure we have a valid log file
    if (!logFile) {
        logFile = fopen(logPath, "a");
        if (!logFile) {
            return; // Can't open log file, give up silently
        }
    } else {
        // Check if file is still valid by trying to write to it
        if (fprintf(logFile, "") < 0) {
            // File was deleted or has an error, close and reset
            fclose(logFile);
            logFile = nullptr;
            logFile = fopen(logPath, "a");
            if (!logFile) {
                return; // Still can't open, give up
            }
        }
    }
    
    // Get current time
    time_t now = time(nullptr);
    struct tm *tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_info);
    
    // Try to write the log entry
    va_list args;
    va_start(args, format);
    
    // Write timestamp
    if (fprintf(logFile, "[%s] ", timestamp) < 0) {
        // File was deleted during operation, try to recreate
        va_end(args);
        fclose(logFile);
        logFile = nullptr;
        logFile = fopen(logPath, "a");
        if (!logFile) {
            return; // Still can't open, give up
        }
        va_start(args, format);
        if (fprintf(logFile, "[%s] ", timestamp) < 0) {
            va_end(args);
            return; // Still failing, give up
        }
    }
    
    // Write the actual log message
    if (vfprintf(logFile, format, args) < 0) {
        // File was deleted during operation, try to recreate
        va_end(args);
        fclose(logFile);
        logFile = nullptr;
        logFile = fopen(logPath, "a");
        if (!logFile) {
            return; // Still can't open, give up
        }
        va_start(args, format);
        if (fprintf(logFile, "[%s] ", timestamp) < 0 || vfprintf(logFile, format, args) < 0) {
            va_end(args);
            return; // Still failing, give up
        }
    }
    va_end(args);
    
    // Add newline
    if (fprintf(logFile, "\n") < 0) {
        // File was deleted during operation, try to recreate
        fclose(logFile);
        logFile = nullptr;
        logFile = fopen(logPath, "a");
        if (!logFile) {
            return; // Still can't open, give up
        }
        va_start(args, format);
        if (fprintf(logFile, "[%s] ", timestamp) < 0 || 
            vfprintf(logFile, format, args) < 0 || 
            fprintf(logFile, "\n") < 0) {
            va_end(args);
            return; // Still failing, give up
        }
        va_end(args);
    }
    
    // Ensure it's written to disk
    fflush(logFile);
}