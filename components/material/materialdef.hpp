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
    // Phase 8a of docs/imgui-overlay-plan.md (and full backend per
    // docs/material-override-plan.md). MaterialDef is a parsed YAML
    // record describing how to override a mesh material at the
    // ShaderVisitor::createProgram seam — swap shaderPrefix, merge
    // defines, push extra uniforms.
    //
    // MVP scope: mesh-only matching (substring on path / diffuse /
    // node name / refid), shader template swap, defines, typed
    // uniforms. Textures + GL state come in Phase 8c.

    using UniformValue = std::variant<float, osg::Vec2f, osg::Vec3f, osg::Vec4f, int, bool>;

    struct UniformDef
    {
        std::string mName;
        UniformValue mValue;
    };

    struct MatchRule
    {
        // A rule fires if at least one filled-in field matches the
        // candidate. Empty fields are ignored. Match is
        // case-insensitive substring.
        std::string mMeshPath;       // matches against the NIF path
        std::string mNodeName;       // matches against osg::Node::getName()
        std::string mTextureSubstr;  // matches against the diffuse texture path
        std::string mRefId;          // exact match (case-insensitive)
    };

    struct MaterialDef
    {
        std::string mName;
        std::vector<MatchRule> mRules; // OR'd at evaluation time

        std::string mShaderPrefix;     // optional; empty = keep default
        std::map<std::string, std::string> mDefines;
        std::vector<UniformDef> mUniforms;

        int mPriority = 0; // higher wins on tie
    };
}

#endif
