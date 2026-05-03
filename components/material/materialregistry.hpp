#ifndef OPENMW_COMPONENTS_MATERIAL_MATERIALREGISTRY_H
#define OPENMW_COMPONENTS_MATERIAL_MATERIALREGISTRY_H

#include <memory>
#include <string>
#include <vector>

#include "materialdef.hpp"

namespace VFS
{
    class Manager;
}

namespace Material
{
    // Registry of MaterialDef records loaded from data/materials/*.yaml
    // (read via the VFS so mod data dirs work). Owned by the
    // Resource::SceneManager and queried at ShaderVisitor::createProgram
    // time. MVP: load-once at boot, no hot-reload.
    class Registry
    {
    public:
        explicit Registry(const VFS::Manager* vfs);
        ~Registry();

        // Returns the highest-priority MaterialDef that matches the
        // given mesh / node / diffuse, or nullptr if none match.
        // The returned pointer is owned by the registry and stable
        // for the registry's lifetime.
        const MaterialDef* matchMesh(const std::string& meshPath, const std::string& nodeName,
            const std::string& diffuseFilename) const;

        // Number of materials successfully loaded (debug / UI).
        std::size_t size() const { return mMaterials.size(); }

    private:
        std::vector<std::unique_ptr<MaterialDef>> mMaterials;
    };
}

#endif
