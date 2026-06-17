#ifndef OPENMW_MP_SERVERPASSWORD_HPP
#define OPENMW_MP_SERVERPASSWORD_HPP

#include <string>
#include <string_view>

namespace mwmp
{
    enum class ServerPasswordValidation
    {
        Accepted,
        AcceptedWithExtraClientPassword,
        Rejected,
    };

    std::string normalizeServerPassword(std::string_view password);
    bool isServerPassworded(std::string_view password);
    ServerPasswordValidation validateServerPassword(std::string_view serverPassword, std::string_view clientPassword);
}

#endif
