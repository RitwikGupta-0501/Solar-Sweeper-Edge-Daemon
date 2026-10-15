#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace sse::util {

class JsonHelper {
public:
    // Extracts a typed value from a JSON object by testing a list of candidate
    // RFC 6901 JSON pointer expressions in priority order (e.g. {"/sector_id", "/sectorId", "/sector"}).
    // If none match or if parsing fails, returns defaultValue.
    template <typename T>
    static T ExtractField(const nlohmann::json& j, const std::vector<std::string>& candidatePointers, T defaultValue) {
        if (!j.is_object()) {
            return defaultValue;
        }

        for (const auto& ptrStr : candidatePointers) {
            try {
                nlohmann::json::json_pointer ptr(ptrStr);
                if (j.contains(ptr)) {
                    const auto& val = j[ptr];
                    if (!val.is_null()) {
                        return val.get<T>();
                    }
                }
            } catch (...) {
                // Ignore pointer format or type conversion errors and proceed to next candidate
            }
        }
        return defaultValue;
    }

    // Helper to parse hex strings like "0x01", "0xFF", or integer values into uint8_t
    static uint8_t ParseByte(const nlohmann::json& val, uint8_t defaultVal = 0) {
        if (val.is_number_integer() || val.is_number_unsigned()) {
            return static_cast<uint8_t>(val.get<unsigned int>() & 0xFF);
        }
        if (val.is_string()) {
            const std::string s = val.get<std::string>();
            try {
                unsigned long parsed = std::stoul(s, nullptr, 0); // handles "0x..", "0..", and decimal
                return static_cast<uint8_t>(parsed & 0xFF);
            } catch (...) {
                return defaultVal;
            }
        }
        return defaultVal;
    }
};

} // namespace sse::util
