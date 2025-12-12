/**
 * @file test_version.cpp
 * @brief Unit tests for version information
 */

#include <gtest/gtest.h>
#include <kcenon/file_transfer/file_transfer.h>

namespace kcenon::file_transfer::test {

class VersionTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(VersionTest, MajorVersionIsCorrect) {
    EXPECT_EQ(version::major, 0);
}

TEST_F(VersionTest, MinorVersionIsCorrect) {
    EXPECT_EQ(version::minor, 2);
}

TEST_F(VersionTest, PatchVersionIsCorrect) {
    EXPECT_EQ(version::patch, 0);
}

TEST_F(VersionTest, VersionStringIsCorrect) {
    EXPECT_EQ(version::to_string(), "0.2.0");
}

TEST_F(VersionTest, VersionStringFormat) {
    auto ver = version::to_string();
    EXPECT_FALSE(ver.empty());

    // Should contain dots
    EXPECT_NE(ver.find('.'), std::string::npos);
}

}  // namespace kcenon::file_transfer::test
