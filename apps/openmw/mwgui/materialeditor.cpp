#include "materialeditor.hpp"

#include <algorithm>
#include <cstring>
#include <utility>
#include <variant>

#include <imgui.h>

#include <components/material/materialdef.hpp>
#include <components/material/materialregistry.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/shader/shadermanager.hpp>
#include <components/vfs/manager.hpp>

#include "../mwbase/environment.hpp"

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
}
