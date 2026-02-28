#pragma once

namespace runtime_policy {

struct TimingConfig {
    int measureInterval;
    int publishInterval;
    int samplesPerPublish;
    bool publishClamped;
};

TimingConfig computeTimingConfig(int measureInterval, int publishInterval, int maxMeasurements);
int clampPublishCount(int publishCount, int samplesPerPublish);
long computePublishWindowSeconds(int samplesToPublish, int measureInterval);
bool canCollectSample(int publishCount, int samplesPerPublish);

} // namespace runtime_policy

