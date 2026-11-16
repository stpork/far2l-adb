from pathlib import Path
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'testing/perf-regressions'))
from common import ROOT, section, check
source = r'''
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cerrno>
static int fault = 0;
static size_t allocated = 0;
int audit_memalign(void **p, size_t a, size_t s) {
    if (fault) return ENOMEM;
    int r = posix_memalign(p, a, s); if (!r) allocated=s; return r;
}
void *audit_malloc(size_t s) {
    if (fault == 1 && s >= 65536) return nullptr;
    void *p = malloc(s); if (p) allocated=s; return p;
}
#define posix_memalign audit_memalign
#define malloc audit_malloc
#include "plugins/NetRocks/src/Op/Utils/IOBuffer.cpp"
#undef malloc
#undef posix_memalign
int main(int argc, char **argv) {
    if (argc > 2) {
        fault=1;
        IOBuffer b(65536,32768,16777216);
        assert(b.Size()==32768 && allocated==32768);
        b.Desire(65536);
        assert(b.Size()==32768);
        printf("OOM fallback: allocated=%zu, advertised=%zu\n", allocated,b.Size());
        fflush(stdout);
        memset(b.Data(), 1, b.Size());
    } else {
        IOBuffer b(65536,32768,16777216);
        fault=2;
        bool result=b.Increase();
        printf("Failed growth: Increase()=%d, size=%zu\n", result,b.Size());
        assert(!result && b.Size()==65536);
    }
}
'''
check('IOBuffer allocation failure and fallback capacity', source)
check('IOBuffer fallback size remains bounded', source, ['oom'])
