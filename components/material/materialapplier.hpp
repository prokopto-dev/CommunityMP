#ifndef OPENMW_COMPONENTS_MATERIAL_MATERIALAPPLIER_H
#define OPENMW_COMPONENTS_MATERIAL_MATERIALAPPLIER_H

#include <map>
#include <string>

#include "materialdef.hpp"

namespace osg
{
    class StateSet;
}

namespace Material
{
    // Apply a MaterialDef to a writable StateSet:
    // - merge defines into the caller's defineMap (caller resolves
    //   shaderPrefix and program after this)
    // - push uniforms (OVERRIDE flag)
    // The caller still owns the StateSet and is responsible for
    // not sharing it across instances (Phase 8c).
    void mergeDefines(const MaterialDef& def, std::map<std::string, std::string>& defineMap);
    void pushUniforms(const MaterialDef& def, osg::StateSet* stateSet);
}

#endif
