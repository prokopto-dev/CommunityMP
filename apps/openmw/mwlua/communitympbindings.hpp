#ifndef MWLUA_COMMUNITYMPBINDINGS_H
#define MWLUA_COMMUNITYMPBINDINGS_H

#include <sol/table.hpp>

namespace MWLua
{
    struct Context;

    sol::table initCommunityMpPackage(const Context& context);
}

#endif // MWLUA_COMMUNITYMPBINDINGS_H
