#ifndef DESKTOP_CARD_STRING_MAKER_H
#define DESKTOP_CARD_STRING_MAKER_H

#include <romanian_whist/Card.h>

#include <catch2/catch_tostring.hpp>

#include <string>

// Without this, a mismatched hand prints as "{ {?}, {?} }" - which says two
// hands differ but not how, on precisely the assertions this suite is made of.
//
// A near-copy of the engine's and the terminal's tests/CardStringMaker.h,
// deliberately: both of those sit in test directories, which are not exported
// include paths, so there is nothing to share short of the engine publishing a
// test-support target. Duplicating eight lines is the cheaper of the two.
//
// It must be VISIBLE in every translation unit that compares Cards: a TU that
// instantiates the primary template while another explicitly specialises it is
// ill-formed with no diagnostic required. Include it at the top of every test
// file. The optional half is handled by CATCH_CONFIG_ENABLE_OPTIONAL_STRINGMAKER,
// set on the test target in CMakeLists.txt.
namespace Catch
{
template<>
struct StringMaker<romanian_whist::Card>
{
    static std::string convert(const romanian_whist::Card& card)
    {
        return card.toString();
    }
};
}

#endif
