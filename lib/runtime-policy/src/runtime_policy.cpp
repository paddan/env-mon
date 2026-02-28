#include "runtime_policy.h"

namespace runtime_policy {

TimingConfig computeTimingConfig(int measureInterval, int publishInterval, int maxMeasurements) {
    TimingConfig timing{};
    timing.measureInterval = measureInterval > 0 ? measureInterval : 1;
    timing.publishInterval = publishInterval >= timing.measureInterval ? publishInterval : timing.measureInterval;
    timing.samplesPerPublish = (timing.publishInterval + timing.measureInterval - 1) / timing.measureInterval;
    if (timing.samplesPerPublish < 1) {
        timing.samplesPerPublish = 1;
    }

    timing.publishClamped = false;
    if (maxMeasurements > 0 && timing.samplesPerPublish > maxMeasurements) {
        timing.samplesPerPublish = maxMeasurements;
        timing.publishClamped = true;
    }
    return timing;
}

int clampPublishCount(int publishCount, int samplesPerPublish) {
    if (publishCount < 0) {
        return 0;
    }
    if (publishCount > samplesPerPublish) {
        return samplesPerPublish;
    }
    return publishCount;
}

long computePublishWindowSeconds(int samplesToPublish, int measureInterval) {
    const int safeSamples = samplesToPublish > 0 ? samplesToPublish : 1;
    const int safeInterval = measureInterval > 0 ? measureInterval : 1;

    long publishWindowSeconds = static_cast<long>(safeSamples) * safeInterval;
    if (publishWindowSeconds <= 0) {
        publishWindowSeconds = safeInterval;
    }
    return publishWindowSeconds;
}

bool canCollectSample(int publishCount, int samplesPerPublish) { return publishCount < samplesPerPublish; }

} // namespace runtime_policy

