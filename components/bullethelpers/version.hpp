#ifndef OPENMW_COMPONENTS_BULLETHELPERS_VERSION_H
#define OPENMW_COMPONENTS_BULLETHELPERS_VERSION_H

#include <LinearMath/btScalar.h>

#define OPENMW_REQUIRED_BULLET_VERSION 325

static_assert(BT_BULLET_VERSION >= OPENMW_REQUIRED_BULLET_VERSION,
    "CommunityMP requires Bullet 3.25 or newer for the current Bullet physics path");

namespace BulletHelpers
{
    inline constexpr int RequiredBulletVersion = OPENMW_REQUIRED_BULLET_VERSION;
    inline constexpr int RuntimeBulletVersion = BT_BULLET_VERSION;
}

#endif
