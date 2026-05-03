#ifndef COMPONENTS_TERRAIN_MATERIAL_H
#define COMPONENTS_TERRAIN_MATERIAL_H

#include <osg/StateSet>
#include <osg/Vec2f>

#include <components/esm/refid.hpp>

namespace osg
{
    class Texture2D;
}

namespace Resource
{
    class SceneManager;
}

namespace Terrain
{

    struct TextureLayer
    {
        osg::ref_ptr<osg::Texture2D> mDiffuseMap;
        osg::ref_ptr<osg::Texture2D> mNormalMap; // optional
        bool mParallax = false;
        bool mSpecular = false;
    };

    std::vector<osg::ref_ptr<osg::StateSet>> createPasses(Resource::SceneManager* sceneManager,
        const std::vector<TextureLayer>& layers, const std::vector<osg::ref_ptr<osg::Texture2D>>& blendmaps,
        int blendmapScale, float layerTileSize, bool isComposite, bool esm4terrain = false,
        bool useTessellation = false, bool useDisplacementEmulation = false,
        const ESM::RefId& worldspace = ESM::RefId(),
        osg::Vec2f chunkCenter = osg::Vec2f(0.0f, 0.0f));
}

#endif
