from common import ROOT, check
prefix=r'''
#include <cassert>
#include <cstdio>
#include <cctype>
#include <ctime>
#include <cstring>
#include <vector>
#include "WinPort/WinCompat.h"
#define WINPORT(n) n
#define TTY_PARSED_WANTMORE size_t(0)
unsigned MapVirtualKey(unsigned,unsigned){return 0;}
struct Handler {void OnUsingExtension(char){}};
struct TTYInputSequenceParser {
    Handler handler;Handler *_handler=&handler;
    unsigned _iterm_last_flags=0;char _using_extension=0;
    int _iterm2_cmd_state=0;time_t _iterm2_cmd_ts=0;
    std::vector<INPUT_RECORD> _ir_pending;
    bool ReadUTF8InHex(const char*,size_t,wchar_t*);
    size_t TryParseAsITerm2EscapeSequence(const char*,size_t);
};

#include <array>
#include <limits>
#include <cerrno>
#include <cstdlib>
#include <random>
#define TTY_PARSED_BADSEQUENCE size_t(-1)
'''
source=(ROOT/"WinPort/src/Backend/TTY/TTYInputSequenceParserExts.cpp").read_text()
source=source[source.index("bool TTYInputSequenceParser::ReadUTF8InHex("):]
main=r'''
int main(){
    TTYInputSequenceParser p;
    for(const char *text:{"c080","eda080","f4908080","4142434445","1","ff","e282","e228ac"}){
        wchar_t c=0x1234;assert(!p.ReadUTF8InHex(text,strlen(text),&c));assert(c==0);
    }
    for(auto valid:{std::string("]1337;d;1;61;0\a"),std::string("]1337;u;1;f09f9880;0\a"),std::string("]1337;f;2\a"),std::string("]1337;d;5;c3a5;0;61\a"),std::string("]1337;d;1;;0x7e\a")}) {
        for(size_t n=0;n<valid.size();++n){std::vector<char> exact(valid.begin(),valid.begin()+n);assert(p.TryParseAsITerm2EscapeSequence(exact.data(),n)==TTY_PARSED_WANTMORE);}
        assert(p.TryParseAsITerm2EscapeSequence(valid.data(),valid.size())==valid.size());
        auto joined=valid+valid;assert(p.TryParseAsITerm2EscapeSequence(joined.data(),joined.size())==valid.size());
    }
    for(auto bad:{std::string("]1337;\a"),std::string("]1337;d;0;61;0\a"),std::string("]1337;d;999999999999999999999;61;0\a"),std::string("]1337;d;1;4142434445;0\a"),std::string("]1337;d;1;61\a"),std::string("]1337;d;5;61;0\a")}) {
        std::vector<char> exact(bad.begin(),bad.end());auto count=p._ir_pending.size();
        assert(p.TryParseAsITerm2EscapeSequence(exact.data(),exact.size())==TTY_PARSED_BADSEQUENCE);assert(p._ir_pending.size()==count);
    }
    std::string huge(256,'a');assert(p.TryParseAsITerm2EscapeSequence(huge.data(),huge.size())==TTY_PARSED_BADSEQUENCE);
    std::mt19937 rng(123);for(int i=0;i<20000;++i){std::vector<char> input(rng()%280);for(char &c:input)c=char(rng());p.TryParseAsITerm2EscapeSequence(input.data(),input.size());}
}
'''
check("Bounded iTerm2 parsing",prefix+source+main)
