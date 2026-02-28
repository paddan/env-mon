#include <mqtt_utils.h>
#include <unity.h>

void test_sanitize_identifier_transforms_expected_characters() {
    auto sanitized = mqtt_utils::sanitizeIdentifier(" Env-Mon 42 ");
    TEST_ASSERT_EQUAL_STRING("env_mon_42", sanitized.c_str());
}

void test_sanitize_identifier_falls_back_to_default() {
    auto sanitized = mqtt_utils::sanitizeIdentifier("???");
    TEST_ASSERT_EQUAL_STRING("envmon", sanitized.c_str());
}

void test_normalize_base_topic_trims_and_strips_slashes() {
    auto normalized = mqtt_utils::normalizeBaseTopic("  sensors/env///  ");
    TEST_ASSERT_EQUAL_STRING("sensors/env", normalized.c_str());
}

void test_normalize_base_topic_uses_default_when_empty() {
    auto normalized = mqtt_utils::normalizeBaseTopic("   ///   ");
    TEST_ASSERT_EQUAL_STRING("env-mon", normalized.c_str());
}

void test_has_required_credentials_rejects_empty_values() {
    TEST_ASSERT_FALSE(mqtt_utils::hasRequiredCredentials("", "secret"));
    TEST_ASSERT_FALSE(mqtt_utils::hasRequiredCredentials("user", ""));
    TEST_ASSERT_FALSE(mqtt_utils::hasRequiredCredentials("   ", "secret"));
    TEST_ASSERT_FALSE(mqtt_utils::hasRequiredCredentials("user", "   "));
}

void test_has_required_credentials_accepts_non_empty_values() {
    TEST_ASSERT_TRUE(mqtt_utils::hasRequiredCredentials("user", "secret"));
    TEST_ASSERT_TRUE(mqtt_utils::hasRequiredCredentials(" user ", " secret "));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_sanitize_identifier_transforms_expected_characters);
    RUN_TEST(test_sanitize_identifier_falls_back_to_default);
    RUN_TEST(test_normalize_base_topic_trims_and_strips_slashes);
    RUN_TEST(test_normalize_base_topic_uses_default_when_empty);
    RUN_TEST(test_has_required_credentials_rejects_empty_values);
    RUN_TEST(test_has_required_credentials_accepts_non_empty_values);
    return UNITY_END();
}
