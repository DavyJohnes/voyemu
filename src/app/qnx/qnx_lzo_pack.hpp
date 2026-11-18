#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace voyemu {
// One LZO1X block packed by a shortest parse over every match the block offers within the format
// distances. emit(grow) re-encodes the first grow two-byte matches of 3 to 8 bytes in their
// three-byte form: each adds exactly one byte, and the block decodes to the same plaintext.
class QnxLzoBlock {
public:
    explicit QnxLzoBlock(std::string_view plain);

    std::size_t bytes() const {
        return bytes_;
    }

    std::size_t growable() const {
        return growable_;
    }

    std::string emit(std::size_t grow = 0) const;

private:
    enum class Kind : std::uint8_t {
        near2,
        far3,
        short_match,
        middle,
        distant
    };
    struct Token {
        std::uint32_t literals, length, distance;
        Kind kind;
    };
    std::string plain_;
    std::vector<Token> tokens_;
    std::size_t bytes_ = 0, growable_ = 0;
};
} // namespace voyemu
