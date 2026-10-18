#pragma once

#include <array>
#include <cstddef>
#include <string>

namespace runtime {

template <typename Character, std::size_t Extent>
std::basic_string<Character> JoinFields(
    const std::array<std::basic_string<Character>, Extent>& fields, Character delimiter
) {
    std::basic_string<Character> record;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index != 0) {
            record.push_back(delimiter);
        }
        record.append(fields[index]);
    }
    return record;
}

} // namespace runtime
