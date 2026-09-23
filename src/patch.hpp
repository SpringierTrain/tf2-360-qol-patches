// Patches for Xbox 360 TF2's Client_360.dll.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace patches {

enum class Status {
    Patched,         // image was modified
    AlreadyPatched,  // our code is already there
    NotFound,        // code not recognised; image untouched
};

struct Result {
    Status status;
    std::string log;  // what was found / changed, one line per item
};

// Each patch works on the flat basefile image in place. `base` is the image
// load address.
using ApplyFn = Result (*)(std::vector<uint8_t>& image, uint32_t base);

struct Patch {
    const char* name;
    const char* summary;
    ApplyFn apply;
};

Result deathnotice(std::vector<uint8_t>& image, uint32_t base);
Result ubercharge(std::vector<uint8_t>& image, uint32_t base);

inline const std::vector<Patch>& all() {
    static const std::vector<Patch> list = {
        {"deathnotice", "dark kill feed text when you're involved", deathnotice},
        {"ubercharge", "UBERCHARGE: %charge% label value", ubercharge},
    };
    return list;
}

}  // namespace patches
