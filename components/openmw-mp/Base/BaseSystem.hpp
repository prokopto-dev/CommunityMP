#ifndef OPENMW_BASESYSTEM_HPP
#define OPENMW_BASESYSTEM_HPP

#include <string>

#include <components/openmw-mp/Transport/PacketIdentity.hpp>

namespace mwmp
{
    class BaseSystem
    {
    public:

        BaseSystem(PacketGuid guid) : guid(guid)
        {

        }

        BaseSystem()
        {

        }

        PacketGuid guid;
        std::string playerName;
        std::string serverPassword;
        std::string accountPasswordHash;

    };
}

#endif //OPENMW_BASESYSTEM_HPP
