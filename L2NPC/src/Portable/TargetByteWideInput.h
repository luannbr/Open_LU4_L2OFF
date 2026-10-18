#pragma once

#include <istream>
#include <streambuf>
#include <string>

namespace l2npc::portable {

// The target MSVC wide file stream gives the UTF-16LE script files to the
// lexers one raw byte per get(), and the lexers join two bytes into one code
// unit. Other hosts decode or reject those bytes in their wchar_t file
// codecvt. This buffer reads a byte stream and returns each byte unchanged.
class TargetByteWideInputBuffer final : public std::wstreambuf {
public:
    explicit TargetByteWideInputBuffer(std::istream& input) : m_Input(input) {}

protected:
    int_type underflow() override {
        if (gptr() && gptr() < egptr()) {
            return traits_type::to_int_type(*gptr());
        }

        const int value = m_Input.get();
        if (value == std::char_traits<char>::eof()) {
            return traits_type::eof();
        }

        m_Current = static_cast<wchar_t>(static_cast<unsigned char>(value));
        setg(&m_Current, &m_Current, &m_Current + 1);
        return traits_type::to_int_type(m_Current);
    }

private:
    std::istream& m_Input;
    wchar_t m_Current = 0;
};

} // namespace l2npc::portable
