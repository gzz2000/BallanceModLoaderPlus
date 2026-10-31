#include <string>

#include <gtest/gtest.h>

#include "Api/Behavior/Text.h"

namespace BML::Api::Behavior {
namespace {

constexpr unsigned Western = 1252;
constexpr unsigned Chinese = 936;

TEST(BehaviorText, KeepsAsciiUnchanged) {
    std::string storage;
    EXPECT_EQ(NativeText("Level_01.NMO", Chinese), "Level_01.NMO");
    EXPECT_EQ(Utf8Text("Level_01.NMO", storage, Chinese), "Level_01.NMO");
}

#if defined(_WIN32)
TEST(BehaviorText, ConvertsToTheCodePageAndBack) {
    const std::string level = "\xe5\x85\xb3\xe5\x8d\xa1";
    const std::string native = NativeText(level, Chinese);
    EXPECT_EQ(native, "\xb9\xd8\xbf\xa8");
    std::string storage;
    EXPECT_EQ(Utf8Text(native, storage, Chinese), level);

    const std::string cafe = "caf\xc3\xa9";
    EXPECT_EQ(NativeText(cafe, Western), "caf\xe9");
    EXPECT_EQ(Utf8Text("caf\xe9", storage, Western), cafe);
}
#else
TEST(BehaviorText, NativeHostPreservesUtf8RegardlessOfWindowsCodePage) {
    const std::string level = "\xe5\x85\xb3\xe5\x8d\xa1";
    const std::string cafe = "caf\xc3\xa9";
    for (unsigned cp : {Chinese, Western}) {
        std::string storage;
        EXPECT_EQ(NativeText(level, cp), level);
        EXPECT_EQ(Utf8Text(level, storage, cp), level);
        EXPECT_EQ(NativeText(cafe, cp), cafe);
        EXPECT_EQ(Utf8Text(cafe, storage, cp), cafe);
    }
}
#endif

TEST(BehaviorText, KeepsTextTheCodePageCannotHold) {
    const std::string pass = "\xe5\x85\xb3";
    EXPECT_EQ(NativeText(pass, Western), pass);
    std::string storage;
    EXPECT_EQ(Utf8Text(pass, storage, Western), pass);
    EXPECT_TRUE(storage.empty());
}

} // namespace
} // namespace BML::Api::Behavior
