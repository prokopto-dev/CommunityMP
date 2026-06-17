#ifndef OPENMW_COMPONENTS_MATERIAL_MATERIALREGISTRY_H
#define OPENMW_COMPONENTS_MATERIAL_MATERIALREGISTRY_H

#include <cstddef>
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
    class Registry
    {
    public:
        explicit Registry(const VFS::Manager* vfs);
        ~Registry();

        void reload(const VFS::Manager* vfs);
        bool loadFile(const std::string& fsPath);

        const MaterialDef* matchMesh(const std::string& meshPath, const std::string& nodeName,
            const std::string& diffuseFilename, const std::string& refId = std::string()) const;

        const MaterialDef* matchTerrain(const std::string& worldspaceLower, int cellX, int cellY) const;

        std::size_t size() const { return mMaterials.size(); }
        void resort();
        bool removeByName(const std::string& name);
        MaterialDef* add(MaterialDef def);

        MaterialDef* at(std::size_t i) { return i < mMaterials.size() ? mMaterials[i].get() : nullptr; }
        const MaterialDef* at(std::size_t i) const
        {
            return i < mMaterials.size() ? mMaterials[i].get() : nullptr;
        }

    private:
        std::vector<std::unique_ptr<MaterialDef>> mMaterials;
    };
}

#endif
