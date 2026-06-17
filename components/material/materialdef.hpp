#ifndef OPENMW_COMPONENTS_MATERIAL_MATERIALDEF_H
#define OPENMW_COMPONENTS_MATERIAL_MATERIALDEF_H

#include <map>
#include <string>
#include <variant>
#include <vector>

#include <osg/Vec2f>
#include <osg/Vec3f>
#include <osg/Vec4f>

namespace Material
{
    using UniformValue = std::variant<float, osg::Vec2f, osg::Vec3f, osg::Vec4f, int, bool>;

    struct UniformDef
    {
        std::string mName;
        UniformValue mValue;
    };

    struct MatchRule
    {
        std::string mMeshPath;
        std::string mNodeName;
        std::string mTextureSubstr;
        std::string mRefId;
    };

    struct TerrainCell
    {
        int mX = 0;
        int mY = 0;
    };

    struct TerrainRule
    {
        std::string mWorldspace;
        std::vector<TerrainCell> mCells;
    };

    struct MaterialDef
    {
        std::string mName;
        std::vector<MatchRule> mRules;
        std::vector<TerrainRule> mTerrainRules;
        std::string mShaderPrefix;
        std::map<std::string, std::string> mDefines;
        std::vector<UniformDef> mUniforms;
        int mPriority = 0;
    };
}

#endif
