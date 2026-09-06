/**
 * Conformance: does this runtime's content_hash agree with the other SDKs?
 *
 * Every expected value below was produced by the Python SDK
 * (iaes.envelope.compute_content_hash) and is reproducible by anyone:
 *
 *     >>> from iaes.envelope import compute_content_hash
 *     >>> compute_content_hash({"measurement_type": "vibration_velocity",
 *     ...                       "value": 1.23, "unit": "mm/s"})
 *     '7fa9644fb4d05913'
 *
 * This is the test that did not exist. Without it the runtime shipped an
 * FNV-1a digest over ArduinoJson's insertion order while the specification
 * asked for SHA-256 over sorted keys, so the same reading hashed differently
 * depending on which SDK produced it -- and a consumer deduplicating across
 * sources kept both copies without any error to notice.
 *
 * Runs on a host, deliberately: agreement between languages cannot be checked
 * on a board.
 *
 * SPDX-License-Identifier: MIT
 */

#include <unity.h>

#include "iaes/IaesCanonical.h"
#include "iaes/IaesHash.h"

#include <ArduinoJson.h>
#include <string.h>

static void hashOf(JsonObjectConst data, char* out) {
    char canonical[512];
    size_t n = iaes::canonicalize(data, canonical, sizeof(canonical));
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, n, "canonicalize refused the payload");
    IaesHash::contentHash(canonical, out, 17);
}

static void canonicalOf(JsonObjectConst data, char* out, size_t len) {
    TEST_ASSERT_NOT_EQUAL(0, iaes::canonicalize(data, out, len));
}

// ─── SHA-256 itself ──────────────────────────────────────────

void test_sha256_matches_the_published_vectors(void) {
    struct { const char* in; const char* want16; } cases[] = {
        // Standard NIST vectors, truncated to the 16 characters the
        // specification uses.
        {"",    "e3b0c44298fc1c14"},
        {"abc", "ba7816bf8f01cfea"},
        // 55, 56 and 64 bytes: the lengths where padding implementations break.
        {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "9f4390f8d30c2dd9"},
        {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "b35439a4ac6f0948"},
        {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "ffe054fe7ae0cb6d"},
    };
    for (auto& c : cases) {
        char got[17];
        IaesHash::contentHash(c.in, got, sizeof(got));
        TEST_ASSERT_EQUAL_STRING(c.want16, got);
    }
}

// ─── Agreement with the Python and TypeScript SDKs ───────────

void test_measurement_hash_matches_python(void) {
    JsonDocument doc;
    JsonObject d = doc.to<JsonObject>();
    d["measurement_type"] = "vibration_velocity";
    d["value"] = 1.23;
    d["unit"] = "mm/s";

    char got[17];
    hashOf(d, got);
    TEST_ASSERT_EQUAL_STRING("7fa9644fb4d05913", got);
}

void test_key_order_does_not_change_the_hash(void) {
    // The same payload, built in the opposite order. ArduinoJson preserves
    // insertion order, so this is exactly what the old digest got wrong.
    JsonDocument doc;
    JsonObject d = doc.to<JsonObject>();
    d["unit"] = "mm/s";
    d["value"] = 1.23;
    d["measurement_type"] = "vibration_velocity";

    char got[17];
    hashOf(d, got);
    TEST_ASSERT_EQUAL_STRING("7fa9644fb4d05913", got);
}

void test_whole_numbers_lose_the_decimal_point(void) {
    // 25600.0 must canonicalize as 25600, or this runtime disagrees with
    // JavaScript, and with the Python SDK, which normalizes the same way.
    JsonDocument doc;
    JsonObject d = doc.to<JsonObject>();
    d["measurement_type"] = "motor_speed";
    d["value"] = 25600.0;
    d["unit"] = "rpm";

    char canonical[256];
    canonicalOf(d, canonical, sizeof(canonical));
    TEST_ASSERT_EQUAL_STRING(
        "{\"measurement_type\":\"motor_speed\",\"unit\":\"rpm\",\"value\":25600}",
        canonical);

    char got[17];
    hashOf(d, got);
    TEST_ASSERT_EQUAL_STRING("88436239f7064c11", got);
}

void test_trailing_zeros_are_dropped(void) {
    // The old builder formatted with "%.2f", so 0.8 was written as "0.80" and
    // hashed differently from every other SDK.
    JsonDocument doc;
    JsonObject d = doc.to<JsonObject>();
    d["measurement_type"] = "current";
    d["value"] = 0.80;
    d["unit"] = "A";

    char got[17];
    hashOf(d, got);
    TEST_ASSERT_EQUAL_STRING("9bd8d6d9a42e022d", got);
}

void test_health_hash_matches_python(void) {
    JsonDocument doc;
    JsonObject d = doc.to<JsonObject>();
    d["health_index"] = 0.5;
    d["severity"] = "high";

    char got[17];
    hashOf(d, got);
    TEST_ASSERT_EQUAL_STRING("4477e2cbcf010047", got);
}

void test_optional_fields_are_part_of_the_hash(void) {
    JsonDocument doc;
    JsonObject d = doc.to<JsonObject>();
    d["health_index"] = 0.25;
    d["severity"] = "critical";
    d["recommended_action"] = "check it";

    char got[17];
    hashOf(d, got);
    TEST_ASSERT_EQUAL_STRING("ff1f2448ee24038b", got);
}

void test_strings_are_escaped_the_same_way(void) {
    JsonDocument doc;
    JsonObject d = doc.to<JsonObject>();
    d["measurement_type"] = "a\"b";
    d["value"] = 1;
    d["unit"] = "x";

    char got[17];
    hashOf(d, got);
    TEST_ASSERT_EQUAL_STRING("8308f10266847701", got);
}

// ─── Refusing rather than truncating ─────────────────────────

void test_a_payload_that_does_not_fit_is_refused(void) {
    // Truncating the canonical form would give two different payloads the same
    // digest, which defeats the only thing the hash is for. The old
    // implementation truncated at 384 bytes and called it "unique enough".
    JsonDocument doc;
    JsonObject d = doc.to<JsonObject>();
    d["recommended_action"] =
        "0123456789012345678901234567890123456789012345678901234567890123456789";

    char small[32];
    TEST_ASSERT_EQUAL(0, iaes::canonicalize(d, small, sizeof(small)));
    TEST_ASSERT_EQUAL_STRING("", small);
}

void test_nested_objects_sort_at_every_level(void) {
    JsonDocument doc;
    JsonObject d = doc.to<JsonObject>();
    JsonObject inner = d["z"].to<JsonObject>();
    inner["b"] = 2;
    inner["a"] = 1;
    d["y"] = 3;

    char canonical[256];
    canonicalOf(d, canonical, sizeof(canonical));
    TEST_ASSERT_EQUAL_STRING("{\"y\":3,\"z\":{\"a\":1,\"b\":2}}", canonical);
}

// Unity calls these around every test. Absent, the MinGW linker refuses --
// the GNU/Linux one did not, so the suite ran in CI and not on Windows.
void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_sha256_matches_the_published_vectors);
    RUN_TEST(test_measurement_hash_matches_python);
    RUN_TEST(test_key_order_does_not_change_the_hash);
    RUN_TEST(test_whole_numbers_lose_the_decimal_point);
    RUN_TEST(test_trailing_zeros_are_dropped);
    RUN_TEST(test_health_hash_matches_python);
    RUN_TEST(test_optional_fields_are_part_of_the_hash);
    RUN_TEST(test_strings_are_escaped_the_same_way);
    RUN_TEST(test_a_payload_that_does_not_fit_is_refused);
    RUN_TEST(test_nested_objects_sort_at_every_level);
    return UNITY_END();
}
