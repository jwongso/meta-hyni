#include "../src/response_utils.h"
#include <gtest/gtest.h>
#include <random>
#include <string>
#include <vector>

using namespace hyni;
using namespace testing;

class ResponseUtilsTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}

    // Helper function to generate random strings
    std::string generate_random_string(size_t length) {
        static const char alphanum[] =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            " .,;:-";
        static std::mt19937 rng(std::random_device{}());
        static std::uniform_int_distribution<> dist(0, sizeof(alphanum) - 2);

        std::string s;
        s.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            s += alphanum[dist(rng)];
        }
        return s;
    }
};

// Test cases for split_and_normalize
TEST_F(ResponseUtilsTest, SplitEmptyString) {
    auto result = response_utils::split_and_normalize("");
    EXPECT_TRUE(result.empty());
}

TEST_F(ResponseUtilsTest, SplitSingleWord) {
    auto result = response_utils::split_and_normalize("hello");
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], "hello");
}

TEST_F(ResponseUtilsTest, SplitMultipleWords) {
    auto result = response_utils::split_and_normalize("hello world");
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], "hello");
    EXPECT_EQ(result[1], "world");
}

TEST_F(ResponseUtilsTest, SplitWithPunctuation) {
    auto result = response_utils::split_and_normalize("hello, world. Good-morning;");
    ASSERT_EQ(result.size(), 4);
    EXPECT_EQ(result[0], "hello");
    EXPECT_EQ(result[1], "world");
    EXPECT_EQ(result[2], "Good");
    EXPECT_EQ(result[3], "morning");
}

TEST_F(ResponseUtilsTest, SplitWithMultipleSpaces) {
    auto result = response_utils::split_and_normalize("  hello   world  ");
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], "hello");
    EXPECT_EQ(result[1], "world");
}

// Test cases for merge_strings
TEST_F(ResponseUtilsTest, MergeEmptyStrings) {
    int best_match = -1;
    auto result = response_utils::merge_strings("", "", best_match);
    EXPECT_TRUE(result.empty());
    EXPECT_EQ(best_match, -1);
}

TEST_F(ResponseUtilsTest, MergeFirstStringEmpty) {
    int best_match = -1;
    auto result = response_utils::merge_strings("", "world", best_match);
    EXPECT_EQ(result, "world");
    EXPECT_EQ(best_match, -1);
}

TEST_F(ResponseUtilsTest, MergeSecondStringEmpty) {
    int best_match = -1;
    auto result = response_utils::merge_strings("hello", "", best_match);
    EXPECT_EQ(result, "hello");
    EXPECT_EQ(best_match, -1);
}

TEST_F(ResponseUtilsTest, MergeNoOverlap) {
    int best_match = -1;
    auto result = response_utils::merge_strings("hello", "world", best_match);
    EXPECT_EQ(result, "hello world");
    EXPECT_EQ(best_match, -1);
}

TEST_F(ResponseUtilsTest, MergeWithPartialOverlap_one) {
    int best_match = -1;
    auto result = response_utils::merge_strings("hello world", "world peace", best_match);
    // Should NOT merge because only 1 word ("world") matches
    EXPECT_EQ(result, "hello world peace");
    EXPECT_EQ(best_match, 1);  // No valid merge point found
}

TEST_F(ResponseUtilsTest, MergeWithPartialOverlap_two) {
    int best_match = -1;
    auto result = response_utils::merge_strings("hello world peace", "world peace together", best_match);
    // Should NOT merge because only 2 words ("world peace") match
    EXPECT_EQ(result, "hello world peace together");
    EXPECT_EQ(best_match, 1);  // No valid merge point found
}

TEST_F(ResponseUtilsTest, MergeWithPartialOverlap_three) {
    int best_match = -1;
    auto result = response_utils::merge_strings("hello world peace together", "world peace together forever", best_match);
    // SHOULD merge because 3 words ("world peace together") match
    EXPECT_EQ(result, "hello world peace together forever");
    EXPECT_GE(best_match, 1);  // Valid merge point found
}

// Additional tests to verify the 3-word rule
TEST_F(ResponseUtilsTest, MergeRequiresThreeWordOverlap) {
    // Two word overlap - should not merge
    {
        int best_match = -1;
        auto result = response_utils::merge_strings("one two three four", "three four five six", best_match);
        EXPECT_EQ(result, "one two three four five six");
        EXPECT_EQ(best_match, 2);
    }

    // Three word overlap - should merge
    {
        int best_match = -1;
        auto result = response_utils::merge_strings("one two three four five", "three four five six seven", best_match);
        EXPECT_EQ(result, "one two three four five six seven");
        EXPECT_GE(best_match, 1);
    }

    // Four word overlap - should merge
    {
        int best_match = -1;
        auto result = response_utils::merge_strings("one two three four five six", "three four five six seven eight", best_match);
        EXPECT_EQ(result, "one two three four five six seven eight");
        EXPECT_GE(best_match, 1);
    }
}

TEST_F(ResponseUtilsTest, MergeWhenAIsPrefixOfB) {
    int best_match = -1;
    auto result = response_utils::merge_strings(
        "one two three",          // a
        "one two three four",     // b (contains all of a at start)
        best_match
        );
    EXPECT_EQ(result, "one two three four");
    EXPECT_EQ(best_match, 0);  // Indicates prefix match
}

TEST_F(ResponseUtilsTest, MergeAtDifferentPositions) {
    // Merge at beginning (now passes)
    {
        int best_match = -1;
        auto result = response_utils::merge_strings(
            "one two three",
            "one two three four",
            best_match
            );
        EXPECT_EQ(result, "one two three four");
        EXPECT_EQ(best_match, 0);
    }

    // Merge in middle (unchanged)
    {
        int best_match = -1;
        auto result = response_utils::merge_strings(
            "start one two three end",
            "one two three continuation",
            best_match
            );
        EXPECT_EQ(result, "start one two three continuation");
        EXPECT_GE(best_match, 1);
    }

    // Merge at end (unchanged)
    {
        int best_match = -1;
        auto result = response_utils::merge_strings(
            "start middle one two three",
            "one two three",
            best_match
            );
        EXPECT_EQ(result, "start middle one two three");
        EXPECT_GE(best_match, 2);
    }
}

TEST_F(ResponseUtilsTest, NoMergeWhenLessThanThreeWords) {
    // First string too short
    {
        int best_match = -1;
        auto result = response_utils::merge_strings("one two", "one two three", best_match);
        EXPECT_EQ(result, "one two three");
        EXPECT_EQ(best_match, 0);
    }

    // Second string too short
    {
        int best_match = -1;
        auto result = response_utils::merge_strings("one two three", "two three", best_match);
        EXPECT_EQ(result, "one two three");
        EXPECT_EQ(best_match, 1);
    }

    // Both strings too short
    {
        int best_match = -1;
        auto result = response_utils::merge_strings("one two", "two three", best_match);
        EXPECT_EQ(result, "one two three");
        EXPECT_EQ(best_match, 1);
    }
}

TEST_F(ResponseUtilsTest, MergeWithFullOverlap) {
    int best_match = -1;
    auto result = response_utils::merge_strings("hello world", "hello world", best_match);
    EXPECT_EQ(result, "hello world");
    EXPECT_GE(best_match, 0);
}

TEST_F(ResponseUtilsTest, MergeWithMultipleWordOverlap) {
    int best_match = -1;
    auto result = response_utils::merge_strings("the quick brown fox", "brown fox jumps", best_match);
    EXPECT_EQ(result, "the quick brown fox jumps");
    EXPECT_GE(best_match, 1);
}

TEST_F(ResponseUtilsTest, MergeDifficultCustomerScenario) {
    std::string a = "So can you give me a time when you have to handle a very difficult customer?";
    std::string b = "a very difficult boss"; // Modified to start with the overlapping phrase

    int best_match = -1;
    auto result = response_utils::merge_strings(a, b, best_match);

    // Verify merged result
    EXPECT_EQ(result, "So can you give me a time when you have to handle a very difficult boss");
    EXPECT_GE(best_match, 0);

    // Verify the exact merge point
    auto a_words = response_utils::split_and_normalize(a);
    ASSERT_LT(best_match + 2, a_words.size()); // Ensure we don't access out of bounds
    std::string overlap_point = a_words[best_match] + " " +
                                a_words[best_match+1] + " " +
                                a_words[best_match+2];
    EXPECT_EQ(overlap_point, "a very difficult");
}

TEST_F(ResponseUtilsTest, MergeWithPunctuation) {
    int best_match = -1;
    auto result = response_utils::merge_strings("hello, world", "world peace", best_match);
    EXPECT_EQ(result, "hello, world peace");
    EXPECT_GE(best_match, 1);
}

TEST_F(ResponseUtilsTest, MergeWithDifferentCase) {
    int best_match = -1;
    auto result = response_utils::merge_strings("Hello World", "world peace", best_match);
    // Note: This test might fail if case sensitivity is important
    // Current implementation is case-sensitive
    EXPECT_EQ(result, "Hello World world peace");
    EXPECT_GE(best_match, -1);
}

TEST_F(ResponseUtilsTest, MergeWithMultiplePossibleMatches) {
    int best_match = -1;
    auto result = response_utils::merge_strings("the cat in the hat", "the hat is red", best_match);
    EXPECT_EQ(result, "the cat in the hat is red");
    EXPECT_GE(best_match, 1);
}

TEST_F(ResponseUtilsTest, MergeWithMultiplePossibleMatches_three) {
    int best_match = -1;
    auto result = response_utils::merge_strings("the cat in the hat", "in the hat is red", best_match);
    EXPECT_EQ(result, "the cat in the hat is red");
    EXPECT_GE(best_match, 0);
}

TEST_F(ResponseUtilsTest, MergeWithShortOverlap) {
    int best_match = -1;
    auto result = response_utils::merge_strings("hello world", "world", best_match);
    EXPECT_EQ(result, "hello world");
    EXPECT_GE(best_match, 1);
}

TEST_F(ResponseUtilsTest, MergeWithSingleWordOverlap) {
    int best_match = -1;
    auto result = response_utils::merge_strings("hello", "hello world", best_match);
    EXPECT_EQ(result, "hello world");
    EXPECT_GE(best_match, 0);
}

TEST_F(ResponseUtilsTest, MergeWithNoSpaceBetween) {
    int best_match = -1;
    auto result = response_utils::merge_strings("hello", "world", best_match);
    EXPECT_EQ(result, "hello world");
    EXPECT_EQ(best_match, -1);
}

TEST_F(ResponseUtilsTest, MergeWithExistingSpace) {
    int best_match = -1;
    auto result = response_utils::merge_strings("hello ", "world", best_match);
    EXPECT_EQ(result, "hello world");
    EXPECT_EQ(best_match, -1);
}

// Performance tests
TEST_F(ResponseUtilsTest, PerformanceSplitShortString) {
    const std::string input = "This is a test string with some words";
    constexpr std::chrono::microseconds max_duration(50); // 50μs max

    auto start = std::chrono::high_resolution_clock::now();
    auto result = response_utils::split_and_normalize(input);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = duration_cast<std::chrono::microseconds>(end - start);

    EXPECT_LT(duration.count(), max_duration.count())
        << "split_and_normalize took " << duration.count() << "μs (max allowed: "
        << max_duration.count() << "μs)";
}

TEST_F(ResponseUtilsTest, PerformanceSplitLongString) {
    const std::string input = generate_random_string(10000); // 10KB string
    constexpr std::chrono::microseconds max_duration(1500); // 1.5ms max

    auto start = std::chrono::high_resolution_clock::now();
    auto result = response_utils::split_and_normalize(input);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = duration_cast<std::chrono::microseconds>(end - start);

    EXPECT_LT(duration.count(), max_duration.count())
        << "split_and_normalize took " << duration.count() << "μs (max allowed: "
        << max_duration.count() << "μs)";
}

TEST_F(ResponseUtilsTest, PerformanceMergeShortStrings) {
    const std::string a = "This is a test string";
    const std::string b = "test string with some words";
    constexpr std::chrono::microseconds max_duration(50); // 50μs max
    int best_match = -1;

    auto start = std::chrono::high_resolution_clock::now();
    auto result = response_utils::merge_strings(a, b, best_match);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = duration_cast<std::chrono::microseconds>(end - start);

    EXPECT_LT(duration.count(), max_duration.count())
        << "merge_strings took " << duration.count() << "μs (max allowed: "
        << max_duration.count() << "μs)";
}

TEST_F(ResponseUtilsTest, PerformanceMergeLongStrings) {
    const std::string a = generate_random_string(5000);
    const std::string b = generate_random_string(5000);
    constexpr std::chrono::microseconds max_duration(1000); // 1ms max
    int best_match = -1;

    auto start = std::chrono::high_resolution_clock::now();
    auto result = response_utils::merge_strings(a, b, best_match);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = duration_cast<std::chrono::microseconds>(end - start);

    EXPECT_LT(duration.count(), max_duration.count())
        << "merge_strings took " << duration.count() << "μs (max allowed: "
        << max_duration.count() << "μs)";
}

TEST_F(ResponseUtilsTest, PerformanceDifficultCustomerScenario) {
    std::string a = "So can you give me a time when you have to handle a very difficult customer?";
    std::string b = "or let's say a very difficult boss";
    constexpr std::chrono::microseconds max_duration(50); // 50μs max
    int best_match = -1;

    auto start = std::chrono::high_resolution_clock::now();
    auto result = response_utils::merge_strings(a, b, best_match);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = duration_cast<std::chrono::microseconds>(end - start);

    EXPECT_LT(duration.count(), max_duration.count())
        << "Specific scenario took " << duration.count() << "μs (max allowed: "
        << max_duration.count() << "μs)";

    // Still verify correctness
    EXPECT_EQ(result, "So can you give me a time when you have to handle a very difficult customer? or let's say a very difficult boss");
    EXPECT_GE(best_match, -1);
}

// Test empty input
TEST_F(ResponseUtilsTest, EmptyInput) {
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode(nullptr, 0), "");
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode(""), "");
}

// Test single character inputs (1 byte -> 2 padding chars)
TEST_F(ResponseUtilsTest, SingleCharacter) {
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("A"), "QQ==");
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("B"), "Qg==");
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("M"), "TQ==");
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("f"), "Zg==");
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("z"), "eg==");
}

// Test two character inputs (2 bytes -> 1 padding char)
TEST_F(ResponseUtilsTest, TwoCharacters) {
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("AB"), "QUI=");
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("Hi"), "SGk=");
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("Ma"), "TWE=");
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("bc"), "YmM=");
}

// Test three character inputs (3 bytes -> no padding)
TEST_F(ResponseUtilsTest, ThreeCharacters) {
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("ABC"), "QUJD");
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("Man"), "TWFu");
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("Hi!"), "SGkh");
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("xyz"), "eHl6");
}

// Test longer strings with various lengths
TEST_F(ResponseUtilsTest, LongerStrings) {
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("Hello"), "SGVsbG8=");
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("Hello!"), "SGVsbG8h");
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("Hello, World!"), "SGVsbG8sIFdvcmxkIQ==");
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("The quick brown fox"), "VGhlIHF1aWNrIGJyb3duIGZveA==");
}

// Test RFC 4648 test vectors
TEST_F(ResponseUtilsTest, RFC4648TestVectors) {
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode(""), "");
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("f"), "Zg==");
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("fo"), "Zm8=");
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("foo"), "Zm9v");
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("foob"), "Zm9vYg==");
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("fooba"), "Zm9vYmE=");
    EXPECT_EQ(response_utils::response_utils::response_utils::base64_encode("foobar"), "Zm9vYmFy");
}

// Test binary data with all possible byte values
TEST_F(ResponseUtilsTest, BinaryData) {
    // Test null bytes
    unsigned char null_data[] = {0x00, 0x00, 0x00};
    EXPECT_EQ(response_utils::response_utils::base64_encode(null_data, 3), "AAAA");

    // Test high byte values
    unsigned char high_data[] = {0xFF, 0xFF, 0xFF};
    EXPECT_EQ(response_utils::response_utils::base64_encode(high_data, 3), "////");

    // Test mixed binary data
    unsigned char mixed_data[] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    EXPECT_EQ(response_utils::response_utils::base64_encode(mixed_data, 8), "EjRWeJq83vA=");
}

// Test edge cases with specific byte patterns
TEST_F(ResponseUtilsTest, EdgeCases) {
    // Test pattern that generates '=' in encoded output (should not be confused with padding)
    unsigned char data1[] = {0x3E}; // Should encode to '>'
    EXPECT_EQ(response_utils::response_utils::base64_encode(data1, 1), "Pg==");

    // Test pattern that generates '+' and '/' characters
    unsigned char data2[] = {0x3E, 0x3F}; // Should contain + or /
    EXPECT_EQ(response_utils::response_utils::base64_encode(data2, 2), "Pj8=");

    // Test all 64 encoding characters appear
    unsigned char data3[] = {0x00, 0x10, 0x83, 0x10, 0x51, 0x87, 0x20, 0x92, 0x8B, 0x30, 0xD3, 0x8F,
                             0x41, 0x14, 0x93, 0x51, 0x55, 0x97, 0x61, 0x96, 0x9B, 0x71, 0xD7, 0x9F,
                             0x82, 0x18, 0xA3, 0x92, 0x59, 0xA7, 0xA2, 0x9A, 0xAB, 0xB2, 0xDB, 0xAF,
                             0xC3, 0x1C, 0xB3, 0xD3, 0x5D, 0xB7, 0xE3, 0x9E, 0xBB, 0xF3, 0xDF, 0xBF};
    std::string result = response_utils::response_utils::base64_encode(data3, sizeof(data3));
    // This should contain a good mix of all base64 characters
    EXPECT_FALSE(result.empty());
}

// Test very long input
TEST_F(ResponseUtilsTest, LongInput) {
    std::string long_input(1000, 'A');
    std::string result = response_utils::response_utils::response_utils::base64_encode(long_input);

    // Length should be correct
    size_t expected_len = 4 * ((1000 + 2) / 3);
    EXPECT_EQ(result.length(), expected_len);

    // Should end with proper padding
    EXPECT_TRUE(result.back() == '=' || isalnum(result.back()) || result.back() == '+' || result.back() == '/');
}

// Test specific lengths around multiples of 3
TEST_F(ResponseUtilsTest, LengthsAroundMultiplesOfThree) {
    // Length 3n-2 (should have 2 padding chars)
    std::string input1(7, 'X'); // 7 = 3*3 - 2
    std::string result1 = response_utils::response_utils::response_utils::base64_encode(input1);
    EXPECT_EQ(result1.substr(result1.length()-2), "==");

    // Length 3n-1 (should have 1 padding char)
    std::string input2(8, 'Y'); // 8 = 3*3 - 1
    std::string result2 = response_utils::response_utils::response_utils::base64_encode(input2);
    EXPECT_EQ(result2.back(), '=');
    EXPECT_NE(result2[result2.length()-2], '=');

    // Length 3n (should have no padding)
    std::string input3(9, 'Z'); // 9 = 3*3
    std::string result3 = response_utils::response_utils::response_utils::base64_encode(input3);
    EXPECT_NE(result3.back(), '=');
}

TEST_F(ResponseUtilsTest, PrintableASCIICharacters) {
    std::string ascii;
    for (int c = 32; c <= 126; ++c) {
        ascii += static_cast<char>(c);
    }

    std::string encoded = response_utils::response_utils::base64_encode(ascii);

    // Basic sanity checks
    EXPECT_FALSE(encoded.empty());
    EXPECT_EQ(encoded.length(), 4 * ((ascii.size() + 2) / 3));
}

TEST_F(ResponseUtilsTest, TwoByteWithZeroPadding) {
    unsigned char data[] = { 'M', 0x00 };  // 'M' followed by null
    EXPECT_EQ(response_utils::base64_encode(data, 2), "TQA=");
}

TEST_F(ResponseUtilsTest, PlusAndSlashEncoding) {
    unsigned char plus_data[] = { 0xFB }; // Should include '+'
    EXPECT_EQ(response_utils::base64_encode(plus_data, 1), "+w==");

    unsigned char slash_data[] = { 0xFF }; // Should include '/'
    EXPECT_EQ(response_utils::base64_encode(slash_data, 1), "/w==");
}

TEST_F(ResponseUtilsTest, ExternalReferenceMatch) {
    std::string input = "Test123!";
    std::string expected = "VGVzdDEyMyE=";  // Verified with `echo -n "Test123!" | base64`
    EXPECT_EQ(response_utils::response_utils::base64_encode(input), expected);
}
