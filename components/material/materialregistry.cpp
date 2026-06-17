#include "materialregistry.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include <yaml-cpp/yaml.h>

#include <components/debug/debuglog.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/recursivedirectoryiterator.hpp>

namespace Material
{
    namespace
    {
        std::string toLower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        bool containsCI(const std::string& haystackLower, const std::string& needleLower)
        {
            return !needleLower.empty() && haystackLower.find(needleLower) != std::string::npos;
        }

        UniformValue parseUniformValue(const std::string& type, const YAML::Node& valueNode)
        {
            if (type == "float")
                return valueNode.as<float>();
            if (type == "int")
                return valueNode.as<int>();
            if (type == "bool")
                return valueNode.as<bool>();
            if (type == "vec2")
            {
                std::vector<float> seq = valueNode.as<std::vector<float>>();
                seq.resize(2, 0.f);
                return osg::Vec2f(seq[0], seq[1]);
            }
            if (type == "vec3")
            {
                std::vector<float> seq = valueNode.as<std::vector<float>>();
                seq.resize(3, 0.f);
                return osg::Vec3f(seq[0], seq[1], seq[2]);
            }
            if (type == "vec4")
            {
                std::vector<float> seq = valueNode.as<std::vector<float>>();
                seq.resize(4, 0.f);
                return osg::Vec4f(seq[0], seq[1], seq[2], seq[3]);
            }

            Log(Debug::Warning) << "[material] unsupported uniform type '" << type << "', using 0.0";
            return 0.f;
        }

        std::unique_ptr<MaterialDef> parseMaterial(const YAML::Node& root, const std::string& sourcePath)
        {
            auto def = std::make_unique<MaterialDef>();
            def->mName = root["name"] ? root["name"].as<std::string>() : sourcePath;

            if (YAML::Node match = root["match"])
            {
                auto pushRule = [&](const YAML::Node& rule) {
                    MatchRule matchRule;
                    if (rule["mesh"])
                        matchRule.mMeshPath = toLower(rule["mesh"].as<std::string>());
                    if (rule["node"])
                        matchRule.mNodeName = toLower(rule["node"].as<std::string>());
                    if (rule["texture"])
                        matchRule.mTextureSubstr = toLower(rule["texture"].as<std::string>());
                    if (rule["record_id"])
                        matchRule.mRefId = toLower(rule["record_id"].as<std::string>());
                    def->mRules.push_back(std::move(matchRule));
                };

                if (match["any"] && match["any"].IsSequence())
                {
                    for (const YAML::Node& rule : match["any"])
                        pushRule(rule);
                }
                else if (!match["terrain"])
                    pushRule(match);

                if (YAML::Node terrain = match["terrain"])
                {
                    TerrainRule terrainRule;
                    if (terrain["worldspace"])
                        terrainRule.mWorldspace = toLower(terrain["worldspace"].as<std::string>());
                    if (YAML::Node cells = terrain["cells"]; cells && cells.IsSequence())
                    {
                        for (const YAML::Node& cellNode : cells)
                        {
                            TerrainCell cell;
                            cell.mX = cellNode["x"] ? cellNode["x"].as<int>() : 0;
                            cell.mY = cellNode["y"] ? cellNode["y"].as<int>() : 0;
                            terrainRule.mCells.push_back(cell);
                        }
                    }
                    def->mTerrainRules.push_back(std::move(terrainRule));
                }
            }

            if (YAML::Node shader = root["shader"])
            {
                if (shader["fragment"])
                    def->mShaderPrefix = shader["fragment"].as<std::string>();
            }

            if (YAML::Node defines = root["defines"])
            {
                for (const auto& kv : defines)
                    def->mDefines[kv.first.as<std::string>()] = kv.second.as<std::string>();
            }

            if (YAML::Node uniforms = root["uniforms"]; uniforms && uniforms.IsSequence())
            {
                for (const YAML::Node& uniformNode : uniforms)
                {
                    UniformDef uniform;
                    uniform.mName = uniformNode["name"].as<std::string>();
                    const std::string type = uniformNode["type"] ? uniformNode["type"].as<std::string>() : "float";
                    uniform.mValue = parseUniformValue(type, uniformNode["value"]);
                    def->mUniforms.push_back(std::move(uniform));
                }
            }

            if (root["priority"])
                def->mPriority = root["priority"].as<int>();

            return def;
        }

        std::vector<std::unique_ptr<MaterialDef>> parseRootNode(
            const YAML::Node& root, const std::string& sourcePath)
        {
            std::vector<std::unique_ptr<MaterialDef>> defs;
            if (YAML::Node materials = root["materials"]; materials && materials.IsSequence())
            {
                int index = 0;
                for (const YAML::Node& material : materials)
                {
                    if (auto def = parseMaterial(material, sourcePath + "#" + std::to_string(index++)))
                        defs.push_back(std::move(def));
                }
            }
            else if (auto def = parseMaterial(root, sourcePath))
                defs.push_back(std::move(def));

            return defs;
        }

        void sortByPriority(std::vector<std::unique_ptr<MaterialDef>>& materials)
        {
            std::stable_sort(materials.begin(), materials.end(),
                [](const auto& left, const auto& right) { return left->mPriority > right->mPriority; });
        }
    }

    Registry::Registry(const VFS::Manager* vfs)
    {
        reload(vfs);
    }

    Registry::~Registry() = default;

    void Registry::reload(const VFS::Manager* vfs)
    {
        mMaterials.clear();
        if (vfs == nullptr)
            return;

        for (const auto& path : vfs->getRecursiveDirectoryIterator("materials/"))
        {
            const std::string source(path.value());
            if (source.size() < 5 || source.compare(source.size() - 5, 5, ".yaml") != 0)
                continue;

            try
            {
                Files::IStreamPtr stream = vfs->get(path);
                std::stringstream buffer;
                buffer << stream->rdbuf();
                YAML::Node root = YAML::Load(buffer.str());

                for (auto& def : parseRootNode(root, source))
                {
                    Log(Debug::Info) << "[material] loaded " << source << " (name=" << def->mName
                                     << ", priority=" << def->mPriority << ")";
                    mMaterials.push_back(std::move(def));
                }
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "[material] failed to parse " << source << ": " << e.what();
            }
        }

        sortByPriority(mMaterials);
    }

    bool Registry::loadFile(const std::string& fsPath)
    {
        try
        {
            std::ifstream in(fsPath);
            if (!in)
                return false;

            std::stringstream buffer;
            buffer << in.rdbuf();
            YAML::Node root = YAML::Load(buffer.str());
            auto defs = parseRootNode(root, fsPath);
            if (defs.empty())
                return false;

            for (auto& def : defs)
            {
                const std::string name = def->mName;
                mMaterials.erase(std::remove_if(mMaterials.begin(), mMaterials.end(),
                                     [&](const auto& material) { return material->mName == name; }),
                    mMaterials.end());
                Log(Debug::Info) << "[material] loaded " << fsPath << " (name=" << def->mName
                                 << ", priority=" << def->mPriority << ")";
                mMaterials.push_back(std::move(def));
            }
            sortByPriority(mMaterials);
            return true;
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "[material] failed to load " << fsPath << ": " << e.what();
            return false;
        }
    }

    void Registry::resort()
    {
        sortByPriority(mMaterials);
    }

    bool Registry::removeByName(const std::string& name)
    {
        const auto oldSize = mMaterials.size();
        mMaterials.erase(std::remove_if(mMaterials.begin(), mMaterials.end(),
                             [&](const auto& material) { return material->mName == name; }),
            mMaterials.end());
        return mMaterials.size() != oldSize;
    }

    MaterialDef* Registry::add(MaterialDef def)
    {
        const std::string name = def.mName;
        mMaterials.erase(std::remove_if(mMaterials.begin(), mMaterials.end(),
                             [&](const auto& material) { return material->mName == name; }),
            mMaterials.end());
        mMaterials.push_back(std::make_unique<MaterialDef>(std::move(def)));
        MaterialDef* result = mMaterials.back().get();
        sortByPriority(mMaterials);
        return result;
    }

    const MaterialDef* Registry::matchTerrain(const std::string& worldspaceLower, int cellX, int cellY) const
    {
        for (const auto& def : mMaterials)
        {
            for (const TerrainRule& rule : def->mTerrainRules)
            {
                if (!rule.mWorldspace.empty() && rule.mWorldspace != worldspaceLower)
                    continue;
                for (const TerrainCell& cell : rule.mCells)
                {
                    if (cell.mX == cellX && cell.mY == cellY)
                        return def.get();
                }
            }
        }
        return nullptr;
    }

    const MaterialDef* Registry::matchMesh(const std::string& meshPath, const std::string& nodeName,
        const std::string& diffuseFilename, const std::string& refId) const
    {
        if (mMaterials.empty())
            return nullptr;

        const std::string meshPathLower = toLower(meshPath);
        const std::string nodeNameLower = toLower(nodeName);
        const std::string diffuseLower = toLower(diffuseFilename);
        const std::string refIdLower = toLower(refId);

        for (const auto& def : mMaterials)
        {
            for (const MatchRule& rule : def->mRules)
            {
                if (containsCI(meshPathLower, rule.mMeshPath) || containsCI(nodeNameLower, rule.mNodeName)
                    || containsCI(diffuseLower, rule.mTextureSubstr) || containsCI(refIdLower, rule.mRefId))
                    return def.get();
            }
        }

        return nullptr;
    }
}
