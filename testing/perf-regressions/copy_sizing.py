from common import check
check('Latency-aware copy sizing',r'''
#include <cassert>
#include <cstdio>
#include "far2l/src/CopyBufferSizer.hpp"
int main(){
for(unsigned latency:{0,150,600,1500}) {
CopyBufferSizer tuner;uint32_t size=65536;uint64_t done=0;unsigned count=0;
while(done<1024ull*1024*1024){uint64_t bytes=std::min<uint64_t>(size,1024ull*1024*1024-done);done+=bytes;++count;
if(bytes==size)size=tuner.Observe(size,8*1024*1024,latency+bytes*1000/(100*1024*1024));
assert(size>=65536 && size<=8*1024*1024 && size%4096==0);}
printf("Model only: latency=%u ms, chunks=%u, final_KiB=%u\n",latency,count,size/1024);
assert(count<300);}
CopyBufferSizer slow;uint32_t size=65536;
for(int i=0;i<100;++i){size=slow.Observe(size,8*1024*1024,size*1000ull/32768);assert(size<=131072);}
CopyBufferSizer collapse;size=8*1024*1024;for(int i=0;i<8;++i)size=collapse.Observe(size,8*1024*1024,10000);assert(size<=131072);
CopyBufferSizer limited;assert(limited.Observe(65536,65536,1)==65536);
}
''')
