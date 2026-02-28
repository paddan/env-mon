#include <runtime_policy.h>
#include <unity.h>

void test_compute_timing_config_rounds_up_samples() {
    auto timing = runtime_policy::computeTimingConfig(10, 61, 120);
    TEST_ASSERT_EQUAL_INT(10, timing.measureInterval);
    TEST_ASSERT_EQUAL_INT(61, timing.publishInterval);
    TEST_ASSERT_EQUAL_INT(7, timing.samplesPerPublish);
    TEST_ASSERT_FALSE(timing.publishClamped);
}

void test_compute_timing_config_clamps_to_buffer_capacity() {
    auto timing = runtime_policy::computeTimingConfig(1, 500, 120);
    TEST_ASSERT_EQUAL_INT(120, timing.samplesPerPublish);
    TEST_ASSERT_TRUE(timing.publishClamped);
}

void test_clamp_publish_count_bounds_to_valid_range() {
    TEST_ASSERT_EQUAL_INT(0, runtime_policy::clampPublishCount(-5, 12));
    TEST_ASSERT_EQUAL_INT(12, runtime_policy::clampPublishCount(20, 12));
    TEST_ASSERT_EQUAL_INT(7, runtime_policy::clampPublishCount(7, 12));
}

void test_compute_publish_window_seconds_uses_safe_defaults() {
    TEST_ASSERT_EQUAL_INT(30, runtime_policy::computePublishWindowSeconds(3, 10));
    TEST_ASSERT_EQUAL_INT(10, runtime_policy::computePublishWindowSeconds(0, 10));
    TEST_ASSERT_EQUAL_INT(3, runtime_policy::computePublishWindowSeconds(3, 0));
}

void test_can_collect_sample_is_false_when_buffer_full() {
    TEST_ASSERT_TRUE(runtime_policy::canCollectSample(5, 6));
    TEST_ASSERT_FALSE(runtime_policy::canCollectSample(6, 6));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_compute_timing_config_rounds_up_samples);
    RUN_TEST(test_compute_timing_config_clamps_to_buffer_capacity);
    RUN_TEST(test_clamp_publish_count_bounds_to_valid_range);
    RUN_TEST(test_compute_publish_window_seconds_uses_safe_defaults);
    RUN_TEST(test_can_collect_sample_is_false_when_buffer_full);
    return UNITY_END();
}

