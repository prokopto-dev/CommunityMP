#include "materialapplier.hpp"

#include <type_traits>

#include <osg/StateSet>
#include <osg/Uniform>

namespace Material
{
    void mergeDefines(const MaterialDef& def, std::map<std::string, std::string>& defineMap)
    {
        for (const auto& [key, value] : def.mDefines)
            defineMap[key] = value;
    }

    void pushUniforms(const MaterialDef& def, osg::StateSet* stateSet)
    {
        if (stateSet == nullptr)
            return;

        for (const UniformDef& uniformDef : def.mUniforms)
        {
            osg::ref_ptr<osg::Uniform> uniform = std::visit(
                [&](const auto& value) -> osg::ref_ptr<osg::Uniform> {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, float> || std::is_same_v<T, int> || std::is_same_v<T, bool>
                        || std::is_same_v<T, osg::Vec2f> || std::is_same_v<T, osg::Vec3f>
                        || std::is_same_v<T, osg::Vec4f>)
                    {
                        return new osg::Uniform(uniformDef.mName.c_str(), value);
                    }
                    else
                        return nullptr;
                },
                uniformDef.mValue);

            if (uniform != nullptr)
                stateSet->addUniform(uniform, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
        }
    }
}
