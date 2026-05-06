#include "materialregistry.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include <yaml-cpp/yaml.h>

#include <components/debug/debuglog.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vfs/recursivedirectoryiterator.hpp>

namespace Material
{
    namespace
    {
        std::string toLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        bool containsCI(const std::string& haystackLower, const std::string& needleLower)
        {
            if (needleLower.empty())
                return false;
            return haystackLower.find(needleLower) != std::string::npos;
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
                auto seq = valueNode.as<std::vector<float>>();
                seq.resize(2, 0.0f);
                return osg::Vec2f(seq[0], seq[1]);
            }
            if (type == "vec3")
            {
                auto seq = valueNode.as<std::vector<float>>();
                seq.resize(3, 0.0f);
                return osg::Vec3f(seq[0], seq[1], seq[2]);
            }
            if (type == "vec4")
            {
                auto seq = valueNode.as<std::vector<float>>();
                seq.resize(4, 0.0f);
                return osg::Vec4f(seq[0], seq[1], seq[2], seq[3]);
            }
            // Default: treat as float so a typo doesn't crash.
            return 0.0f;
        }

        std::unique_ptr<MaterialDef> parseMaterial(const YAML::Node& root, const std::string& sourcePath)
        {
            auto def = std::make_unique<MaterialDef>();
            def->mName = root["name"] ? root["name"].as<std::string>() : sourcePath;

            if (auto match = root["match"])
            {
                // Mesh / texture / refid rules — `any: [...]` list or
                // a single inline rule.
                auto pushRule = [&](const YAML::Node& r) {
                    MatchRule mr;
                    if (r["mesh"])
                        mr.mMeshPath = toLower(r["mesh"].as<std::string>());
                    if (r["node"])
                        mr.mNodeName = toLower(r["node"].as<std::string>());
                    if (r["texture"])
                        mr.mTextureSubstr = toLower(r["texture"].as<std::string>());
                    if (r["record_id"])
                        mr.mRefId = toLower(r["record_id"].as<std::string>());
                    def->mRules.push_back(std::move(mr));
                };
                if (match["any"] && match["any"].IsSequence())
                {
                    for (const auto& r : match["any"])
                        pushRule(r);
                }
                else if (!match["terrain"])
                {
                    pushRule(match);
                }

                // Phase 8d — terrain rule, separate node:
                //   match:
                //     terrain:
                //       worldspace: morrowind
                //       cells: [{ x: -3, y: -10 }, { x: -3, y: -9 }]
                if (auto terrain = match["terrain"])
                {
                    TerrainRule tr;
                    if (terrain["worldspace"])
                        tr.mWorldspace = toLower(terrain["worldspace"].as<std::string>());
                    if (auto cells = terrain["cells"]; cells && cells.IsSequence())
                    {
                        for (const auto& c : cells)
                        {
                            TerrainCell tc;
                            tc.mX = c["x"] ? c["x"].as<int>() : 0;
                            tc.mY = c["y"] ? c["y"].as<int>() : 0;
                            tr.mCells.push_back(tc);
                        }
                    }
                    def->mTerrainRules.push_back(std::move(tr));
                }
            }

            if (auto shader = root["shader"])
            {
                if (shader["fragment"])
                    def->mShaderPrefix = shader["fragment"].as<std::string>();
            }

            if (auto defines = root["defines"])
            {
                for (const auto& kv : defines)
                    def->mDefines[kv.first.as<std::string>()] = kv.second.as<std::string>();
            }

            if (auto uniforms = root["uniforms"]; uniforms && uniforms.IsSequence())
            {
                for (const auto& u : uniforms)
                {
                    UniformDef ud;
                    ud.mName = u["name"].as<std::string>();
                    const std::string type = u["type"] ? u["type"].as<std::string>() : "float";
                    ud.mValue = parseUniformValue(type, u["value"]);
                    def->mUniforms.push_back(std::move(ud));
                }
            }

            if (root["priority"])
                def->mPriority = root["priority"].as<int>();

            return def;
        }

        // Phase 8b-quinquies — a YAML file may contain either a single
        // MaterialDef at the root, or a `materials:` sequence of them
        // (used by EntityInspector when an entity has overrides on
        // several material slots). Returns the parsed defs in source
        // order; an empty vector signals an unparseable file.
        std::vector<std::unique_ptr<MaterialDef>> parseRootNode(
            const YAML::Node& root, const std::string& sourcePath)
        {
            std::vector<std::unique_ptr<MaterialDef>> out;
            if (auto materials = root["materials"]; materials && materials.IsSequence())
            {
                int idx = 0;
                for (const auto& m : materials)
                {
                    const std::string subPath = sourcePath + "#" + std::to_string(idx++);
                    if (auto def = parseMaterial(m, subPath))
                        out.push_back(std::move(def));
                }
            }
            else if (auto def = parseMaterial(root, sourcePath))
            {
                out.push_back(std::move(def));
            }
            return out;
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

        // Walk the VFS for all *.yaml under "materials/". Tolerant
        // loading: a malformed file logs a warning and is skipped,
        // not a fatal error.
        for (const auto& path : vfs->getRecursiveDirectoryIterator("materials/"))
        {
            const std::string str(path.value());
            if (str.size() < 5 || str.compare(str.size() - 5, 5, ".yaml") != 0)
                continue;
            try
            {
                Files::IStreamPtr stream = vfs->get(path);
                std::stringstream buffer;
                buffer << stream->rdbuf();
                YAML::Node root = YAML::Load(buffer.str());
                for (auto& def : parseRootNode(root, str))
                {
                    Log(Debug::Info) << "[material] loaded " << str << " (name=" << def->mName
                                     << ", priority=" << def->mPriority << ")";
                    mMaterials.push_back(std::move(def));
                }
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "[material] failed to parse " << str << ": " << e.what();
            }
        }

        // Sort by priority desc so matchMesh's first-hit wins.
        std::stable_sort(mMaterials.begin(), mMaterials.end(),
            [](const auto& a, const auto& b) { return a->mPriority > b->mPriority; });
    }

    void Registry::resort()
    {
        std::stable_sort(mMaterials.begin(), mMaterials.end(),
            [](const auto& a, const auto& b) { return a->mPriority > b->mPriority; });
    }

    bool Registry::removeByName(const std::string& name)
    {
        const auto before = mMaterials.size();
        mMaterials.erase(std::remove_if(mMaterials.begin(), mMaterials.end(),
                             [&](const auto& p) { return p->mName == name; }),
            mMaterials.end());
        return mMaterials.size() != before;
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
            // Replace existing entries with matching names so a saved
            // override doesn't pile duplicates on each click — applied
            // per def so a multi-def file refreshes all its entries.
            for (auto& def : defs)
            {
                const std::string newName = def->mName;
                mMaterials.erase(std::remove_if(mMaterials.begin(), mMaterials.end(),
                                     [&](const auto& p) { return p->mName == newName; }),
                    mMaterials.end());
                Log(Debug::Info) << "[material] loaded " << fsPath << " (name=" << def->mName
                                 << ", priority=" << def->mPriority << ")";
                mMaterials.push_back(std::move(def));
            }
            std::stable_sort(mMaterials.begin(), mMaterials.end(),
                [](const auto& a, const auto& b) { return a->mPriority > b->mPriority; });
            return true;
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "[material] failed to load " << fsPath << ": " << e.what();
            return false;
        }
    }

    MaterialDef* Registry::add(MaterialDef def)
    {
        // Replace any existing entry with the same name so consecutive
        // "Create override" clicks on the same slot don't accumulate
        // ghost defs in the registry.
        const std::string name = def.mName;
        mMaterials.erase(std::remove_if(mMaterials.begin(), mMaterials.end(),
                             [&](const auto& p) { return p->mName == name; }),
            mMaterials.end());
        mMaterials.push_back(std::make_unique<MaterialDef>(std::move(def)));
        MaterialDef* ptr = mMaterials.back().get();
        std::stable_sort(mMaterials.begin(), mMaterials.end(),
            [](const auto& a, const auto& b) { return a->mPriority > b->mPriority; });
        return ptr;
    }

    const MaterialDef* Registry::matchTerrain(
        const std::string& worldspaceLower, int cellX, int cellY) const
    {
        for (const auto& def : mMaterials)
        {
            for (const auto& rule : def->mTerrainRules)
            {
                if (!rule.mWorldspace.empty() && rule.mWorldspace != worldspaceLower)
                    continue;
                for (const auto& cell : rule.mCells)
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
            for (const auto& rule : def->mRules)
            {
                if (containsCI(meshPathLower, rule.mMeshPath))
                    return def.get();
                if (containsCI(nodeNameLower, rule.mNodeName))
                    return def.get();
                if (containsCI(diffuseLower, rule.mTextureSubstr))
                    return def.get();
                if (containsCI(refIdLower, rule.mRefId))
                    return def.get();
            }
        }
        return nullptr;
    }
}
