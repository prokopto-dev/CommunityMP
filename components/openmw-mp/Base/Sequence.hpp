#ifndef OPENMW_MP_SEQUENCE_HPP
#define OPENMW_MP_SEQUENCE_HPP

#include <cstdint>

namespace mwmp
{
    inline bool isNewerSequence(std::uint32_t incoming, std::uint32_t current)
    {
        const std::uint32_t delta = incoming - current;
        return delta != 0u && delta < 0x80000000u;
    }
}

#endif
