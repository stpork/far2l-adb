from common import check

check('Clipboard payload length with and without terminators', r'''
#include "WinPort/WinPort.h"
#include <cassert>

template<class Char> void Check() {
    const Char payload[] = {'a', 0, 'b', 0};
    for (size_t count = 0; count <= 4; ++count) {
        const auto result = ClipboardCharsAndCountWithoutNUL<Char>(payload, count * sizeof(Char));
        assert(result.first == payload);
        assert(result.second == (count && !payload[count - 1] ? count - 1 : count));
    }
    const Char exact[] = {'x'};
    assert(ClipboardCharsAndCountWithoutNUL<Char>(exact, sizeof(exact)).second == 1);
}
int main() { Check<char>(); Check<wchar_t>(); }
''')
