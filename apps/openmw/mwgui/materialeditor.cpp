#include "materialeditor.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>

#include <imgui.h>

#include <osg/Image>
#include <osg/Node>
#include <osg/NodeVisitor>
#include <osg/StateSet>
#include <osg/Texture>

#include <components/debug/debuglog.hpp>
#include <components/material/materialdef.hpp>
#include <components/material/materialregistry.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/util.hpp>
#include <components/shader/shadermanager.hpp>
#include <components/vfs/manager.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwrender/objects.hpp"
#include "../mwrender/renderingmanager.hpp"

namespace MWGui
{
    namespace
    {
        bool drawUniformWidgetImpl(Material::UniformDef& u);
    }

    namespace
    {
        // Edit a std::string in-place via ImGui::InputText, growing a
        // local fixed-size buffer (ImGui's std::string overload is
        // gated behind imgui_stdlib.h which we don't vendor). 256
        // chars covers a shader name or a define value comfortably.
        bool inputStringInline(const char* label, std::string& s)
        {
            char buf[256];
            const std::size_t n = std::min<std::size_t>(s.size(), sizeof(buf) - 1);
            std::memcpy(buf, s.data(), n);
            buf[n] = '\0';
            if (ImGui::InputText(label, buf, sizeof(buf)))
            {
                s.assign(buf);
                return true;
            }
            return false;
        }
    }

    bool drawMaterialDefInline(Material::MaterialDef& def)
    {
        bool changed = false;

        // --- Priority + shader prefix (Phase 8b-ter §1) ----------
        if (ImGui::DragInt("Priority", &def.mPriority, 1.0f, -1000, 1000))
            changed = true;
        if (inputStringInline("Shader prefix", def.mShaderPrefix))
            changed = true;

        // --- Defines map (§2) -------------------------------------
        ImGui::TextDisabled("Defines:");
        ImGui::PushID("defines");
        std::string defineToErase;
        std::pair<std::string, std::string> defineToRename;
        bool defineRenamed = false;
        int defineIdx = 0;
        for (auto& [k, v] : def.mDefines)
        {
            ImGui::PushID(defineIdx++);
            std::string key = k;
            std::string val = v;
            ImGui::SetNextItemWidth(120.0f);
            const bool keyEdited = inputStringInline("##key", key);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            const bool valEdited = inputStringInline("##val", val);
            ImGui::SameLine();
            const bool eraseClicked = ImGui::Button("x");
            ImGui::PopID();
            if (eraseClicked)
                defineToErase = k;
            else if (keyEdited && key != k)
            {
                defineToRename = { k, key };
                defineRenamed = true;
                if (valEdited)
                    v = val;
            }
            else if (valEdited && val != v)
            {
                v = val;
                changed = true;
            }
        }
        ImGui::PopID();
        if (!defineToErase.empty())
        {
            def.mDefines.erase(defineToErase);
            changed = true;
        }
        if (defineRenamed)
        {
            auto it = def.mDefines.find(defineToRename.first);
            if (it != def.mDefines.end())
            {
                std::string oldVal = std::move(it->second);
                def.mDefines.erase(it);
                def.mDefines.emplace(defineToRename.second, std::move(oldVal));
            }
            changed = true;
        }
        // "+ Add define" button — shoves an empty key=value pair the
        // user can rename in place.
        if (ImGui::Button("+ Add define"))
        {
            std::string base = "DEFINE";
            std::string candidate = base;
            int n = 1;
            while (def.mDefines.find(candidate) != def.mDefines.end())
                candidate = base + "_" + std::to_string(n++);
            def.mDefines.emplace(candidate, "1");
            changed = true;
        }

        // --- Bump map (Morrowind env-map distortion) --------------
        // Convenience editor for the two uniforms a bump map needs:
        // bumpMapMatrix (mat2 — repacked from Vec4 by pushUniforms)
        // and envMapLumaBias (vec2). NIF-declared bumps already push
        // these from NiTextureProperty; auto-detected bumps get
        // identity defaults from ShaderVisitor. Editing here pushes
        // OVERRIDE uniforms that beat both.
        {
            auto findUniform = [&](const std::string& name) -> Material::UniformDef* {
                for (auto& u : def.mUniforms)
                    if (u.mName == name)
                        return &u;
                return nullptr;
            };
            auto* bumpMat = findUniform("bumpMapMatrix");
            auto* lumaBias = findUniform("envMapLumaBias");

            ImGui::TextDisabled("Bump map (env-map perturbation + normal source):");
            ImGui::PushID("bumpmap");

            // Toggle: prefer bump map over normal map. Drives the
            // shader's "#if @normalMap && !@bumpMapPriority" gate;
            // when on, even meshes with a real normalMap fall through
            // to the bumpMap-as-normal path (objects.frag #elif
            // @bumpMap). YAML overrides can set this directly via
            // `defines: { bumpMapPriority: "1" }` — same key.
            const auto kPriorityKey = std::string("bumpMapPriority");
            auto pIt = def.mDefines.find(kPriorityKey);
            bool priorityOn = (pIt != def.mDefines.end() && pIt->second == "1");
            if (ImGui::Checkbox("Prefer bump map over normal map", &priorityOn))
            {
                if (priorityOn)
                    def.mDefines[kPriorityKey] = "1";
                else
                    def.mDefines.erase(kPriorityKey);
                changed = true;
            }

            // bumpMapMatrix as 2x2 (xx xy / yx yy). Stored as Vec4 in
            // UniformDef; shader binds it as mat2 via pushUniforms.
            osg::Vec4f mat = bumpMat && std::holds_alternative<osg::Vec4f>(bumpMat->mValue)
                ? std::get<osg::Vec4f>(bumpMat->mValue)
                : osg::Vec4f(1.0f, 0.0f, 0.0f, 1.0f);
            float row0[2] = { mat.x(), mat.y() };
            float row1[2] = { mat.z(), mat.w() };
            bool matEdited = false;
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::DragFloat2("bumpMapMatrix row 0 (xx, xy)", row0, 0.01f))
                matEdited = true;
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::DragFloat2("bumpMapMatrix row 1 (yx, yy)", row1, 0.01f))
                matEdited = true;
            if (matEdited)
            {
                osg::Vec4f packed(row0[0], row0[1], row1[0], row1[1]);
                if (bumpMat == nullptr)
                {
                    Material::UniformDef u;
                    u.mName = "bumpMapMatrix";
                    u.mValue = packed;
                    def.mUniforms.push_back(std::move(u));
                }
                else
                    bumpMat->mValue = packed;
                changed = true;
            }

            // envMapLumaBias = (scale, bias) — bumpTex.b * scale + bias
            osg::Vec2f bias = lumaBias && std::holds_alternative<osg::Vec2f>(lumaBias->mValue)
                ? std::get<osg::Vec2f>(lumaBias->mValue)
                : osg::Vec2f(1.0f, 0.0f);
            float biasArr[2] = { bias.x(), bias.y() };
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::DragFloat2("envMapLumaBias (scale, bias)", biasArr, 0.01f))
            {
                osg::Vec2f packed(biasArr[0], biasArr[1]);
                if (lumaBias == nullptr)
                {
                    Material::UniformDef u;
                    u.mName = "envMapLumaBias";
                    u.mValue = packed;
                    def.mUniforms.push_back(std::move(u));
                }
                else
                    lumaBias->mValue = packed;
                changed = true;
            }

            // Clear button — useful to fall back to NIF/auto defaults.
            if ((bumpMat || lumaBias) && ImGui::Button("Clear bump uniforms"))
            {
                def.mUniforms.erase(
                    std::remove_if(def.mUniforms.begin(), def.mUniforms.end(),
                        [](const Material::UniformDef& u) {
                            return u.mName == "bumpMapMatrix" || u.mName == "envMapLumaBias";
                        }),
                    def.mUniforms.end());
                changed = true;
            }
            ImGui::PopID();
        }

        // --- Uniforms (read-only metadata + editable widgets) -----
        ImGui::TextDisabled("Uniforms:");
        ImGui::PushID("uniforms");
        int eraseUniformAt = -1;
        for (std::size_t i = 0; i < def.mUniforms.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Button("x"))
                eraseUniformAt = static_cast<int>(i);
            ImGui::SameLine();
            if (drawUniformWidgetImpl(def.mUniforms[i]))
                changed = true;
            ImGui::PopID();
        }
        ImGui::PopID();
        if (eraseUniformAt >= 0)
        {
            def.mUniforms.erase(def.mUniforms.begin() + eraseUniformAt);
            changed = true;
        }

        // "+ Add uniform" — type picker dropdown next to a name input.
        // Stored in a static so the user's typing survives the next
        // frame; one shared editor state across all materials is fine
        // (only one is interactively visible at a time per pane).
        static char sNewUniformName[64] = "";
        static int sNewUniformType = 0;
        const char* kTypeNames[] = { "float", "int", "bool", "vec2", "vec3", "vec4" };
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputTextWithHint("##newuname", "name", sNewUniformName, sizeof(sNewUniformName));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::Combo("##newutype", &sNewUniformType, kTypeNames, IM_ARRAYSIZE(kTypeNames));
        ImGui::SameLine();
        if (ImGui::Button("+ Add uniform") && sNewUniformName[0] != '\0')
        {
            Material::UniformDef u;
            u.mName = sNewUniformName;
            switch (sNewUniformType)
            {
                case 0: u.mValue = 0.0f; break;
                case 1: u.mValue = 0; break;
                case 2: u.mValue = false; break;
                case 3: u.mValue = osg::Vec2f(0.0f, 0.0f); break;
                case 4: u.mValue = osg::Vec3f(1.0f, 1.0f, 1.0f); break;
                case 5: u.mValue = osg::Vec4f(1.0f, 1.0f, 1.0f, 1.0f); break;
            }
            def.mUniforms.push_back(std::move(u));
            sNewUniformName[0] = '\0';
            changed = true;
        }

        // --- Match rules (read-only for now) ----------------------
        if (!def.mRules.empty())
        {
            ImGui::TextDisabled("Match rules:");
            for (const auto& r : def.mRules)
            {
                if (!r.mMeshPath.empty())
                    ImGui::BulletText("mesh contains: %s", r.mMeshPath.c_str());
                if (!r.mNodeName.empty())
                    ImGui::BulletText("node contains: %s", r.mNodeName.c_str());
                if (!r.mTextureSubstr.empty())
                    ImGui::BulletText("texture contains: %s", r.mTextureSubstr.c_str());
                if (!r.mRefId.empty())
                    ImGui::BulletText("refId: %s", r.mRefId.c_str());
            }
        }
        return changed;
    }

    namespace
    {
        bool drawUniformWidgetImpl(Material::UniformDef& u)
        {
            return std::visit(
                [&](auto&& v) -> bool {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, float>)
                    {
                        return ImGui::DragFloat(u.mName.c_str(), &v, 0.01f);
                    }
                    else if constexpr (std::is_same_v<T, int>)
                    {
                        return ImGui::DragInt(u.mName.c_str(), &v);
                    }
                    else if constexpr (std::is_same_v<T, bool>)
                    {
                        return ImGui::Checkbox(u.mName.c_str(), &v);
                    }
                    else if constexpr (std::is_same_v<T, osg::Vec2f>)
                    {
                        float arr[2] = { v.x(), v.y() };
                        if (ImGui::DragFloat2(u.mName.c_str(), arr, 0.01f))
                        {
                            v.set(arr[0], arr[1]);
                            return true;
                        }
                        return false;
                    }
                    else if constexpr (std::is_same_v<T, osg::Vec3f>)
                    {
                        float arr[3] = { v.x(), v.y(), v.z() };
                        if (ImGui::ColorEdit3(u.mName.c_str(), arr))
                        {
                            v.set(arr[0], arr[1], arr[2]);
                            return true;
                        }
                        return false;
                    }
                    else if constexpr (std::is_same_v<T, osg::Vec4f>)
                    {
                        float arr[4] = { v.x(), v.y(), v.z(), v.w() };
                        if (ImGui::ColorEdit4(u.mName.c_str(), arr))
                        {
                            v.set(arr[0], arr[1], arr[2], arr[3]);
                            return true;
                        }
                        return false;
                    }
                    return false;
                },
                u.mValue);
        }
    }

    void MaterialEditor::draw()
    {
        ImGui::SetNextWindowSize(ImVec2(420.0f, 480.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Materials"))
        {
            ImGui::End();
            return;
        }

        auto* sceneMgr = MWBase::Environment::get().getResourceSystem()->getSceneManager();
        auto* registry = sceneMgr ? sceneMgr->getMaterialRegistry() : nullptr;
        if (registry == nullptr || registry->size() == 0)
        {
            ImGui::TextDisabled(
                "No materials loaded. Add files under data/materials/*.yaml.");
            ImGui::End();
            return;
        }

        // Collect raw pointers — Registry::matchMesh API doesn't expose
        // the list, so we walk via a temporary string-based query.
        // For Phase 8b MVP we add a lightweight enumeration accessor.
        // (A proper accessor lives on Material::Registry::list()).
        ImGui::Text("Loaded: %zu material(s)", registry->size());
        ImGui::SameLine();
        if (ImGui::Button("Reload shaders"))
            sceneMgr->getShaderManager().triggerShaderReload();
        ImGui::SameLine();
        // Phase 8b-quater — full pass: re-runs ShaderVisitor on every
        // loaded object. Picks up newly auto-loaded textures
        // (_n / _spec / _nh) AND re-pushes registry uniforms onto
        // live StateSets. Heavier than triggerShaderReload (which only
        // recompiles GLSL) — use this when adding files to the overlay
        // dir or after editing uniforms.
        if (ImGui::Button("Recreate shaders (full)"))
        {
            if (auto* rendering = MWBase::Environment::get().getWorld()->getRenderingManager())
                if (auto* root = rendering->getObjects().getRootNode())
                    sceneMgr->recreateShaders(root);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload from disk"))
        {
            registry->reload(MWBase::Environment::get().getResourceSystem()->getVFS());
            sceneMgr->getShaderManager().triggerShaderReload();
        }

        ImGui::Separator();

        std::string toDelete;
        for (std::size_t i = 0; i < registry->size(); ++i)
        {
            auto* def = registry->at(i);
            if (def == nullptr)
                continue;
            ImGui::PushID(static_cast<int>(i));
            const std::string label = def->mName + "  (priority " + std::to_string(def->mPriority) + ")";
            if (ImGui::CollapsingHeader(label.c_str()))
            {
                ImGui::Indent();
                if (ImGui::Button("Delete"))
                    toDelete = def->mName;
                else if (drawMaterialDefInline(*def))
                {
                    registry->resort();
                    sceneMgr->getShaderManager().triggerShaderReload();
                }
                ImGui::Unindent();
            }
            ImGui::PopID();
        }
        if (!toDelete.empty())
        {
            registry->removeByName(toDelete);
            sceneMgr->getShaderManager().triggerShaderReload();
        }

        ImGui::End();
    }

    namespace
    {
        // Strip everything outside [a-zA-Z0-9_.-]; used for both YAML
        // material names and on-disk filenames so a refId like
        // "Daedric_Dagger" survives unchanged but path separators or
        // shell metachars get neutralised.
        std::string sanitiseToken(const std::string& s)
        {
            std::string out;
            out.reserve(s.size());
            for (char c : s)
            {
                const unsigned char uc = static_cast<unsigned char>(c);
                if (std::isalnum(uc) || c == '_' || c == '.' || c == '-')
                    out += c;
                else
                    out += '_';
            }
            if (out.empty())
                out = "_";
            return out;
        }

        // Extract the basename of an OSG / VFS-style path: strip the
        // last '/' or '\\' segment. NIF and texture paths in the
        // engine use forward-slash but Morrowind data ships with
        // backslashes — handle both.
        std::string basenameOf(const std::string& path)
        {
            const auto pos = path.find_last_of("/\\");
            return pos == std::string::npos ? path : path.substr(pos + 1);
        }

        // Multi-slot variant of EntityInspector's MaterialProbe: keeps
        // walking after the first match and collects every distinct
        // StateSet. NodePath is built by hand because OSG only fills
        // NodeVisitor::getNodePath() under IntersectionVisitor.
        struct MaterialSlotProbe : public osg::NodeVisitor
        {
            MaterialSlotProbe()
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
            {
            }

            std::vector<MaterialSlot> mSlots;
            std::unordered_set<const osg::StateSet*> mSeen;
            std::vector<std::string> mPathStack;

            void apply(osg::Node& node) override
            {
                mPathStack.push_back(node.getName());
                if (node.getStateSet())
                    inspect(*node.getStateSet(), node.getName(), /*drawableName*/ std::string());
                traverse(node);
                mPathStack.pop_back();
            }

            void apply(osg::Drawable& drawable) override
            {
                if (drawable.getStateSet())
                    inspect(*drawable.getStateSet(), /*candidateName*/ std::string(), drawable.getName());
            }

            std::string joinPath() const
            {
                std::string out;
                for (const auto& seg : mPathStack)
                {
                    if (seg.empty())
                        continue;
                    out += '/';
                    out += seg;
                }
                return out;
            }

            void inspect(const osg::StateSet& ss, const std::string& nodeName, const std::string& drawableName)
            {
                if (!mSeen.insert(&ss).second)
                    return;

                MaterialSlot slot;
                slot.mStateSetKey = &ss;
                slot.mNodePath = joinPath();
                slot.mDrawableName = drawableName;

                // Last non-empty path segment wins as the canonical
                // node name (used for Per-child match rules). If the
                // StateSet sits on a drawable rather than an OSG node
                // we may end up with no name at all — flag mAnonymous
                // so the UI can grey out the Per-child checkbox.
                for (auto it = mPathStack.rbegin(); it != mPathStack.rend(); ++it)
                {
                    if (!it->empty())
                    {
                        slot.mNodeName = *it;
                        break;
                    }
                }
                if (slot.mNodeName.empty() && !nodeName.empty())
                    slot.mNodeName = nodeName;
                slot.mAnonymous = slot.mNodeName.empty();

                // Texture classification: same logic as MaterialProbe
                // (entityinspector.cpp:118-167) — type-driven dispatch
                // via SceneUtil::getTextureType so unit indices don't
                // lie when a slot is missing.
                const auto& list = ss.getTextureAttributeList();
                const osg::Texture* normalTex = nullptr;
                for (unsigned int unit = 0; unit < list.size(); ++unit)
                {
                    const osg::Texture* tex = dynamic_cast<const osg::Texture*>(
                        ss.getTextureAttribute(unit, osg::StateAttribute::TEXTURE));
                    if (tex == nullptr || tex->getImage(0) == nullptr)
                        continue;
                    std::string type = SceneUtil::getTextureType(ss, *tex, unit);
                    const std::string& filename = tex->getImage(0)->getFileName();
                    if (type.empty() && unit == 0)
                        type = "diffuseMap";
                    if (type == "diffuseMap")
                        slot.mDiffuse = filename;
                    else if (type == "normalMap" || type == "normalHeightMap")
                    {
                        slot.mNormal = filename;
                        normalTex = tex;
                    }
                    else if (type == "specularMap")
                        slot.mSpecular = filename;
                    else if (type == "bumpMap")
                        slot.mBump = filename;
                }

                // FX shells: emit the slot if at least one classified
                // texture is present, even when there's no diffuse —
                // some weather/particle materials live this way.
                const bool hasAny = !slot.mDiffuse.empty() || !slot.mNormal.empty()
                    || !slot.mSpecular.empty() || !slot.mBump.empty();
                if (!hasAny)
                    return;

                if (normalTex != nullptr)
                {
                    const auto* img = normalTex->getImage(0);
                    const GLenum fmt = img->getPixelFormat();
                    slot.mHasHeightInNormalAlpha = (fmt == GL_RGBA || fmt == GL_BGRA);
                    if (!slot.mHasHeightInNormalAlpha)
                    {
                        std::string lower = slot.mNormal;
                        std::transform(lower.begin(), lower.end(), lower.begin(),
                            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        if (lower.find("_nh.") != std::string::npos
                            || lower.find("_nh_") != std::string::npos)
                            slot.mHasHeightInNormalAlpha = true;
                    }
                }

                mSlots.push_back(std::move(slot));
            }
        };

        // YAML scalar quoting: single-quoted, doubling literal '. Same
        // helper the EntityInspector "Save as YAML override" used to
        // inline; lifted here so the writer can stay terse.
        std::string yamlQuote(const std::string& s)
        {
            std::string out = "'";
            for (char c : s)
            {
                if (c == '\'')
                    out += "''";
                else
                    out += c;
            }
            out += '\'';
            return out;
        }
    }

    std::vector<MaterialSlot> collectMaterialSlots(osg::Node& root)
    {
        MaterialSlotProbe probe;
        root.accept(probe);
        return std::move(probe.mSlots);
    }

    Material::MaterialDef makeMaterialDefForSlot(const MaterialSlot& slot, std::uint32_t scopeFlags,
        const std::string& refId, const std::string& meshBasename, float parallaxScaleSeed)
    {
        Material::MaterialDef def;
        // Slot key: prefer node name, fall back to diffuse basename so
        // anonymous-node + Per-texture-only overrides still get a
        // stable filename. Empty fallback yields "_" via sanitiseToken.
        std::string slotKey = slot.mNodeName;
        if (slotKey.empty())
            slotKey = basenameOf(slot.mDiffuse);
        def.mName = sanitiseToken(refId) + "__" + sanitiseToken(slotKey);
        def.mPriority = 100;

        // One MatchRule per scope flag — they OR together at lookup
        // time (Registry::matchMesh returns on first match within a
        // def's mRules vector). Empty fields in a rule are ignored,
        // so a single-field rule is the right shape per scope.
        if ((scopeFlags & Scope_PerChild) && !slot.mNodeName.empty())
        {
            Material::MatchRule r;
            r.mNodeName = slot.mNodeName;
            def.mRules.push_back(std::move(r));
        }
        if ((scopeFlags & Scope_PerTexture) && !slot.mDiffuse.empty())
        {
            Material::MatchRule r;
            r.mTextureSubstr = basenameOf(slot.mDiffuse);
            def.mRules.push_back(std::move(r));
        }
        if ((scopeFlags & Scope_PerRecord) && !refId.empty())
        {
            Material::MatchRule r;
            r.mRefId = refId;
            def.mRules.push_back(std::move(r));
        }
        if ((scopeFlags & Scope_PerMesh) && !meshBasename.empty())
        {
            Material::MatchRule r;
            r.mMeshPath = meshBasename;
            def.mRules.push_back(std::move(r));
        }

        // Fallback: if the user picked only Per-child on an anonymous
        // node (so it got dropped) we'd land with zero rules — which
        // would never match. Add a refId rule as a safety net.
        if (def.mRules.empty() && !refId.empty())
        {
            Material::MatchRule r;
            r.mRefId = refId;
            def.mRules.push_back(std::move(r));
        }

        // Seed with parallaxScale at the global default so the editor
        // has something visible to drag immediately. The user can
        // delete it if they want a pure shader-prefix override.
        Material::UniformDef u;
        u.mName = "parallaxScale";
        u.mValue = parallaxScaleSeed;
        def.mUniforms.push_back(std::move(u));
        return def;
    }

    bool writeEntityOverrideYaml(const std::filesystem::path& path, const std::string& refId,
        const std::vector<const Material::MaterialDef*>& defs)
    {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            Log(Debug::Warning) << "[material] mkdir failed for " << path.string() << ": " << ec.message();
            return false;
        }

        std::ofstream out(path);
        if (!out)
        {
            Log(Debug::Warning) << "[material] open failed: " << path.string();
            return false;
        }

        out << "# Generated by EntityInspector → Save as YAML override.\n";
        out << "# Entity refId: " << refId << "\n";
        out << "materials:\n";
        for (const Material::MaterialDef* def : defs)
        {
            if (def == nullptr)
                continue;
            out << "  - name: " << yamlQuote(def->mName) << "\n";
            out << "    priority: " << def->mPriority << "\n";

            if (!def->mRules.empty())
            {
                out << "    match:\n";
                out << "      any:\n";
                for (const auto& r : def->mRules)
                {
                    // Skip fully empty rules; otherwise emit each
                    // populated key on its own mapping element so the
                    // parser at materialregistry.cpp:74-81 can pick
                    // them up. Multiple keys in one entry are AND-less
                    // in this schema (each non-empty field counts as
                    // an independent OR), so a single-key entry is
                    // the unambiguous shape.
                    if (r.mMeshPath.empty() && r.mNodeName.empty() && r.mTextureSubstr.empty()
                        && r.mRefId.empty())
                        continue;
                    out << "        -";
                    bool first = true;
                    auto emitKey = [&](const char* key, const std::string& val) {
                        if (val.empty())
                            return;
                        if (first)
                        {
                            out << " " << key << ": " << yamlQuote(val) << "\n";
                            first = false;
                        }
                        else
                        {
                            out << "          " << key << ": " << yamlQuote(val) << "\n";
                        }
                    };
                    emitKey("mesh", r.mMeshPath);
                    emitKey("node", r.mNodeName);
                    emitKey("texture", r.mTextureSubstr);
                    emitKey("record_id", r.mRefId);
                }
            }

            if (!def->mShaderPrefix.empty())
            {
                out << "    shader:\n";
                out << "      fragment: " << yamlQuote(def->mShaderPrefix) << "\n";
            }

            if (!def->mDefines.empty())
            {
                out << "    defines:\n";
                for (const auto& [k, v] : def->mDefines)
                    out << "      " << k << ": " << yamlQuote(v) << "\n";
            }

            if (!def->mUniforms.empty())
            {
                out << "    uniforms:\n";
                for (const auto& u : def->mUniforms)
                {
                    std::visit(
                        [&](auto&& v) {
                            using T = std::decay_t<decltype(v)>;
                            out << "      - { name: " << u.mName << ", type: ";
                            if constexpr (std::is_same_v<T, float>)
                                out << "float, value: " << v;
                            else if constexpr (std::is_same_v<T, int>)
                                out << "int, value: " << v;
                            else if constexpr (std::is_same_v<T, bool>)
                                out << "bool, value: " << (v ? "true" : "false");
                            else if constexpr (std::is_same_v<T, osg::Vec2f>)
                                out << "vec2, value: [" << v.x() << ", " << v.y() << "]";
                            else if constexpr (std::is_same_v<T, osg::Vec3f>)
                                out << "vec3, value: [" << v.x() << ", " << v.y() << ", " << v.z()
                                    << "]";
                            else if constexpr (std::is_same_v<T, osg::Vec4f>)
                                out << "vec4, value: [" << v.x() << ", " << v.y() << ", " << v.z()
                                    << ", " << v.w() << "]";
                            out << " }\n";
                        },
                        u.mValue);
                }
            }
        }
        out.close();
        Log(Debug::Info) << "[material] wrote " << defs.size() << " def(s) to " << path.string();
        return true;
    }
}
