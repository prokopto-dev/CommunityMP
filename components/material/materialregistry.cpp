#include "materialregistry.hpp"

#include <algorithm>
#include <cctype>
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
                // Support `any: [ ... ]` list of rules, or a single
                // inline rule.
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
                else
                {
                    pushRule(match);
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
    }

    Registry::Registry(const VFS::Manager* vfs)
    {
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
                if (auto def = parseMaterial(root, str))
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

    Registry::~Registry() = default;

    const MaterialDef* Registry::matchMesh(const std::string& meshPath, const std::string& nodeName,
        const std::string& diffuseFilename) const
    {
        if (mMaterials.empty())
            return nullptr;

        const std::string meshPathLower = toLower(meshPath);
        const std::string nodeNameLower = toLower(nodeName);
        const std::string diffuseLower = toLower(diffuseFilename);

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
                // mRefId comparison goes through the Phase 8c plumb
                // (refId stamped on the node) — skipped in MVP.
            }
        }
        return nullptr;
    }
}
