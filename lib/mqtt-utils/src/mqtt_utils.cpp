#include "mqtt_utils.h"

#include <algorithm>
#include <cctype>

namespace {
void trimWhitespace(std::string &value) {
    const auto isSpace = [](unsigned char c) {
        switch (c) {
        case ' ':
        case '\f':
        case '\n':
        case '\r':
        case '\t':
        case '\v':
            return true;
        default:
            return false;
        }
    };

    auto begin = std::find_if_not(value.begin(), value.end(), isSpace);
    auto end = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    if (begin >= end) {
        value.clear();
        return;
    }
    value.assign(begin, end);
}
} // namespace

namespace mqtt_utils {

std::string sanitizeIdentifier(const std::string &input) {
    std::string sanitized;
    sanitized.reserve(input.size());

    bool lastWasUnderscore = false;
    for (unsigned char c : input) {
        if (std::isalnum(c)) {
            sanitized.push_back(static_cast<char>(std::tolower(c)));
            lastWasUnderscore = false;
        } else if (c == '-' || c == '_' ||
                   c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!sanitized.empty() && !lastWasUnderscore) {
                sanitized.push_back('_');
                lastWasUnderscore = true;
            }
        }
    }

    if (!sanitized.empty() && sanitized.back() == '_') {
        sanitized.pop_back();
    }
    if (sanitized.empty()) {
        sanitized = "envmon";
    }
    return sanitized;
}

std::string normalizeBaseTopic(const std::string &topic) {
    std::string base(topic);
    trimWhitespace(base);
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    if (base.empty()) {
        base = "env-mon";
    }
    return base;
}

bool hasRequiredCredentials(const std::string &username, const std::string &password) {
    std::string user = username;
    std::string pass = password;
    trimWhitespace(user);
    trimWhitespace(pass);
    return !user.empty() && !pass.empty();
}

} // namespace mqtt_utils
