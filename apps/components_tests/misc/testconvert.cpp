#include <components/misc/convert.hpp>

#include <gtest/gtest.h>

#include <limits>

namespace
{
    bool isFinite(const osg::Quat& quat)
    {
        return std::isfinite(quat.x()) && std::isfinite(quat.y()) && std::isfinite(quat.z())
            && std::isfinite(quat.w());
    }

    bool isFinite(const btQuaternion& quat)
    {
        return std::isfinite(quat.x()) && std::isfinite(quat.y()) && std::isfinite(quat.z())
            && std::isfinite(quat.w());
    }

    TEST(MiscConvertTest, nonFiniteEulerAnglesBecomeZeroAngleRotations)
    {
        const float rotation[3] = {
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity(),
        };

        const osg::Quat osgRotation = Misc::Convert::makeOsgQuat(rotation);
        const btQuaternion bulletRotation = Misc::Convert::makeBulletQuaternion(rotation);

        EXPECT_TRUE(isFinite(osgRotation));
        EXPECT_TRUE(isFinite(bulletRotation));
        EXPECT_EQ(osgRotation, osg::Quat());
        EXPECT_EQ(bulletRotation, btQuaternion::getIdentity());
    }
}
