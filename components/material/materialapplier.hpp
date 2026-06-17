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
    void mergeDefines(const MaterialDef& def, std::map<std::string, std::string>& defineMap);
    void pushUniforms(const MaterialDef& def, osg::StateSet* stateSet);
}

#endif
