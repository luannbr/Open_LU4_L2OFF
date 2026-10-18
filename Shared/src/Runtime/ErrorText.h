#pragma once

#include <string>
#include <utility>

namespace runtime {

inline void SetErrorText(std::string* output, std::string value) {
    if (output) {
        *output = std::move(value);
    }
}

} // namespace runtime
