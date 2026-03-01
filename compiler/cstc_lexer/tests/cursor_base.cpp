#include <cassert>
#include <cstc_lexer/cursor.hpp>

using namespace cstc::lexer;

int main() {
    Cursor cursor("Hello!");
    const auto ch1 = cursor.first();
    const auto ch2 = cursor.second();
    const auto ch3 = cursor.third();

    assert(ch1 == 'H');
    assert(ch2 == 'e');
    assert(ch3 == 'l');

    assert(cursor.bump() == 'H');
    const auto ch4 = cursor.third();
    assert(ch4 == 'l');

    assert(cursor.bump() == 'e');
    assert(cursor.bump() == 'l');
    const auto ch5 = cursor.third();
    assert(ch5 == '!');

    assert(cursor.bump() == 'l');
    assert(cursor.bump() == 'o');
    assert(cursor.bump() == '!');
    const auto ch6 = cursor.first();
    assert(ch6 == Cursor::EOF_CHAR);
}
