#include "materialapplier.hpp"

#include <osg/StateSet>
#include <osg/Uniform>

namespace Material
{
    void mergeDefines(const MaterialDef& def, std::map<std::string, std::string>& defineMap)
    {
        for (const auto& [k, v] : def.mDefines)
            defineMap[k] = v;
    }

    void pushUniforms(const MaterialDef& def, osg::StateSet* stateSet)
    {
        if (stateSet == nullptr)
            return;
        for (const auto& u : def.mUniforms)
        {
            osg::ref_ptr<osg::Uniform> uniform = std::visit(
                [&](auto&& v) -> osg::ref_ptr<osg::Uniform> {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, float>)
                        return new osg::Uniform(u.mName.c_str(), v);
                    else if constexpr (std::is_same_v<T, int>)
                        return new osg::Uniform(u.mName.c_str(), v);
                    else if constexpr (std::is_same_v<T, bool>)
                        return new osg::Uniform(u.mName.c_str(), v);
                    else if constexpr (std::is_same_v<T, osg::Vec2f>)
                        return new osg::Uniform(u.mName.c_str(), v);
                    else if constexpr (std::is_same_v<T, osg::Vec3f>)
                        return new osg::Uniform(u.mName.c_str(), v);
                    else if constexpr (std::is_same_v<T, osg::Vec4f>)
                        return new osg::Uniform(u.mName.c_str(), v);
                    else
                        return nullptr;
                },
                u.mValue);
            if (uniform != nullptr)
                stateSet->addUniform(uniform, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
        }
    }
}
