#include "materialeditor.hpp"

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
        bool drawUniformWidget(Material::UniformDef& u)
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
                if (!def->mShaderPrefix.empty())
                    ImGui::Text("shader: %s", def->mShaderPrefix.c_str());
                if (!def->mDefines.empty())
                {
                    ImGui::TextDisabled("Defines:");
                    for (const auto& [k, v] : def->mDefines)
                        ImGui::BulletText("%s = %s", k.c_str(), v.c_str());
                }
                if (!def->mUniforms.empty())
                {
                    ImGui::TextDisabled("Uniforms:");
                    bool changed = false;
                    for (auto& u : def->mUniforms)
                        changed |= drawUniformWidget(u);
                    if (changed)
                        sceneMgr->getShaderManager().triggerShaderReload();
                }
                if (!def->mRules.empty())
                {
                    ImGui::TextDisabled("Match rules:");
                    for (const auto& r : def->mRules)
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
                ImGui::Unindent();
            }
            ImGui::PopID();
        }

        ImGui::End();
    }
}
