#ifndef OPENMW_MP_BRANDING_HPP
#define OPENMW_MP_BRANDING_HPP

#include <components/openmw-mp/Version.hpp>

namespace mwmp::Branding
{
    constexpr const char* productName = "CommunityMP";
    constexpr const char* productVersion = TES3MP_VERSION;
    constexpr const char* launcherExecutableName = "communitymp";
    constexpr const char* executableName = "communitymp-client";
    constexpr const char* serverExecutableName = "communitymp-server";
    constexpr const char* hubExecutableName = "communitymp-hub";
    constexpr const char* masterExecutableName = "masterserver";
    constexpr const char* compatibilityName = "TES3MP";
    constexpr const char* defaultGameHost = "play.communitymp.com";
    constexpr const char* defaultMasterHost = "master.communitymp.com";
    constexpr const char* websiteUrl = "https://communitymp.com";
}

#endif
