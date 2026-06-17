#include "ServerPassword.hpp"

#include <components/openmw-mp/Version.hpp>

std::string mwmp::normalizeServerPassword(std::string_view password)
{
    if (password.empty())
        return TES3MP_DEFAULT_PASSW;

    return std::string(password);
}

bool mwmp::isServerPassworded(std::string_view password)
{
    return password != TES3MP_DEFAULT_PASSW;
}

mwmp::ServerPasswordValidation mwmp::validateServerPassword(std::string_view serverPassword,
    std::string_view clientPassword)
{
    if (clientPassword == serverPassword)
        return ServerPasswordValidation::Accepted;

    if (isServerPassworded(serverPassword))
        return ServerPasswordValidation::Rejected;

    return ServerPasswordValidation::AcceptedWithExtraClientPassword;
}
