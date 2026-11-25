// SPDX-License-Identifier: MIT

#include <cassert>
#include <cstring>
#include <common/str.h>

int main()
{
    // Read-only string literals must never be mutated in-place (previously SIGBUS on macOS).
    const char *gold = "Gold";
    const char *cast = "Cast:";
    const char *reagents = "Black Pearl\nBlood Moss\nSulfurous Ash";
    assert(str_lower(gold) == "gold");
    assert(std::strcmp(gold, "Gold") == 0);
    assert(str_lower(cast) == "cast:");
    assert(std::strcmp(cast, "Cast:") == 0);
    assert(str_lower(reagents) == "black pearl\nblood moss\nsulfurous ash");
    assert(str_lower(static_cast<const char *>(nullptr)).empty());

    // astr_t overload returns a lowercased copy without mutating the source.
    const astr_t original = "Bandage 5.0s";
    assert(str_lower(original) == "bandage 5.0s");
    assert(original == "Bandage 5.0s");

    // Mutable char* overload preserves in-place lowercasing for C-buffer callers (uop/mft/vfs).
    char buf[] = "ArtLegacyMUL.UOP";
    char *ret = str_lower(buf);
    assert(ret == buf);
    assert(std::strcmp(buf, "artlegacymul.uop") == 0);

    return 0;
}
