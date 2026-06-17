#include <components/nifosg/particle.hpp>

#include <gtest/gtest.h>

#include <osg/BoundingBox>

namespace
{
    TEST(NifOsgParticleSystemTest, authoredInitialBoundOverridesDynamicParticleBounds)
    {
        NifOsg::ParticleSystem particleSystem;

        osg::BoundingBox authoredBound;
        authoredBound.expandBy(osg::Vec3f(-1.f, -2.f, -3.f));
        authoredBound.expandBy(osg::Vec3f(1.f, 2.f, 3.f));
        particleSystem.setInitialBound(authoredBound);

        osgParticle::Particle particleTemplate;
        particleTemplate.setLifeTime(10.f);
        osgParticle::Particle* particle = particleSystem.createParticle(&particleTemplate);
        ASSERT_NE(particle, nullptr);

        particle->setPosition(osg::Vec3f(10000.f, 10000.f, 10000.f));
        particle->setSizeRange(osgParticle::rangef(500.f, 500.f));

        const osg::BoundingBox computedBound = particleSystem.computeBoundingBox();

        EXPECT_FLOAT_EQ(computedBound.xMin(), authoredBound.xMin());
        EXPECT_FLOAT_EQ(computedBound.yMin(), authoredBound.yMin());
        EXPECT_FLOAT_EQ(computedBound.zMin(), authoredBound.zMin());
        EXPECT_FLOAT_EQ(computedBound.xMax(), authoredBound.xMax());
        EXPECT_FLOAT_EQ(computedBound.yMax(), authoredBound.yMax());
        EXPECT_FLOAT_EQ(computedBound.zMax(), authoredBound.zMax());
    }
}
