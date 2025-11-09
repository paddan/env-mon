#pragma once

#include <string>

namespace mqtt_utils {

// Converts human-friendly device names into lowercase MQTT-safe identifiers.
std::string sanitizeIdentifier(const std::string &input);

// Trims whitespace, strips trailing slashes, and applies a default topic when empty.
std::string normalizeBaseTopic(const std::string &topic);

} // namespace mqtt_utils
