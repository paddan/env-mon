#pragma once

#include <string>

namespace mqtt_utils {

// Converts human-friendly device names into lowercase MQTT-safe identifiers.
std::string sanitizeIdentifier(const std::string &input);

// Trims whitespace, strips trailing slashes, and applies a default topic when empty.
std::string normalizeBaseTopic(const std::string &topic);

// Returns true only when both username and password are non-empty after trimming.
bool hasRequiredCredentials(const std::string &username, const std::string &password);

} // namespace mqtt_utils
