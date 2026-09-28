from common import section, check
prefix=r'''
#include <ctime>
#include <cassert>
#include <limits>
#include "WinPort/WinCompat.h"
#define WINPORT(n) n
#define WINPORT_DECL(n,t,a) t n a
BOOL SystemTimeToFileTime(const SYSTEMTIME*,FILETIME*);
BOOL FileTimeToSystemTime(const FILETIME*,SYSTEMTIME*);
'''
source=section('WinPort/src/APITime.cpp','#define TICKSPERSEC','static int LocalMinusUTC()')
source+=section('WinPort/src/APITime.cpp','WINPORT_DECL(FileTimeToSystemTime,','WINPORT_DECL(FileTimeToDosDateTime,')
main=r'''
int main(){
    for(time_t sec:{time_t(-1),time_t(0),time_t(1700000000)}) {
        for(long ns:{0L,99L,100L,123456789L,999999999L,1000000100L,-100L}) {
            timespec in{sec,ns},out{};FILETIME f{};FileTime_UnixToWin32(in,&f);FileTime_Win32ToUnix(&f,&out);
            int64_t expected=int64_t(sec)*10000000+ns/100;
            if(ns<0 && ns%100) --expected;
            assert(int64_t(out.tv_sec)*10000000+out.tv_nsec/100==expected);
        }
    }
    if(sizeof(time_t)>=8) {
        timespec in{std::numeric_limits<time_t>::max(),1000000000},out{};FILETIME f{};
        FileTime_UnixToWin32(in,&f);assert(f.dwHighDateTime==0xffffffff && f.dwLowDateTime==0xffffffff);
        FileTime_Win32ToUnix(&f,&out);FILETIME again{};FileTime_UnixToWin32(out,&again);
        assert(f.dwHighDateTime==again.dwHighDateTime && f.dwLowDateTime==again.dwLowDateTime);
        in={std::numeric_limits<time_t>::min(),-1};FileTime_UnixToWin32(in,&f);
        assert(!f.dwHighDateTime && !f.dwLowDateTime);
    }
    SYSTEMTIME s{};s.wMonth=2;s.wDay=29;FILETIME ft{};
    for(int year:{1900,2023,2100}){s.wYear=year;assert(!SystemTimeToFileTime(&s,&ft));}
    for(int year:{1604,2000,2024}){s.wYear=year;assert(SystemTimeToFileTime(&s,&ft));}
    s.wDay=28;s.wYear=2023;assert(SystemTimeToFileTime(&s,&ft));
}
'''
check('Gregorian leap-day validation',prefix+'\n#include <initializer_list>\n'+source+main)
