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
    SYSTEMTIME s{};s.wMonth=2;s.wDay=29;FILETIME ft{};
    for(int year:{1900,2023,2100}){s.wYear=year;assert(!SystemTimeToFileTime(&s,&ft));}
    for(int year:{1604,2000,2024}){s.wYear=year;assert(SystemTimeToFileTime(&s,&ft));}
    s.wDay=28;s.wYear=2023;assert(SystemTimeToFileTime(&s,&ft));
}
'''
check('Gregorian leap-day validation',prefix+'\n#include <initializer_list>\n'+source+main)
