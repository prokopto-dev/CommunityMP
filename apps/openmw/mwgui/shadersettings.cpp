#include "shadersettings.hpp"

#include <algorithm>
#include <cstring>

#include <imgui.h>

#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/settings/values.hpp>
#include <components/shader/shadermanager.hpp>

#include "../mwbase/environment.hpp"

namespace MWGui
{
    namespace
    {
        bool inputStringInline(const char* label, std::string& s)
        {
            char buf[128];
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

    void ShaderSettings::draw()
    {
        ImGui::SetNextWindowSize(ImVec2(420.0f, 280.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Shader Settings"))
        {
            ImGui::End();
            return;
        }

        bool changed = false;
        auto& s = Settings::shaders();

        // ----- Toggles ------------------------------------------------
        bool autoNormal = s.mAutoUseObjectNormalMaps.get();
        if (ImGui::Checkbox("Auto use object normal maps", &autoNormal))
        {
            s.mAutoUseObjectNormalMaps.set(autoNormal);
            changed = true;
        }
        bool autoSpec = s.mAutoUseObjectSpecularMaps.get();
        if (ImGui::Checkbox("Auto use object specular maps", &autoSpec))
        {
            s.mAutoUseObjectSpecularMaps.set(autoSpec);
            changed = true;
        }
        ImGui::TextDisabled(
            "Parallax mapping fires automatically when a *_nh.dds is present.");

        ImGui::Separator();

        // ----- Global parallax knobs ---------------------------------
        float pScale = s.mParallaxScale.get();
        if (ImGui::DragFloat("Parallax scale", &pScale, 0.001f, 0.0f, 0.5f, "%.3f"))
        {
            s.mParallaxScale.set(pScale);
            changed = true;
        }
        float pBias = s.mParallaxBias.get();
        if (ImGui::DragFloat("Parallax bias", &pBias, 0.001f, -0.5f, 0.5f, "%.3f"))
        {
            s.mParallaxBias.set(pBias);
            changed = true;
        }

        ImGui::Separator();

        // ----- Patterns ----------------------------------------------
        std::string nrm = s.mNormalMapPattern.get();
        if (inputStringInline("Normal map pattern", nrm))
        {
            s.mNormalMapPattern.set(nrm);
            changed = true;
        }
        std::string nh = s.mNormalHeightMapPattern.get();
        if (inputStringInline("Normal+height pattern", nh))
        {
            s.mNormalHeightMapPattern.set(nh);
            changed = true;
        }
        std::string spec = s.mSpecularMapPattern.get();
        if (inputStringInline("Specular map pattern", spec))
        {
            s.mSpecularMapPattern.set(spec);
            changed = true;
        }

        ImGui::Separator();
        ImGui::TextDisabled("Settings live in ~/Library/Preferences/openmw/settings.cfg.");
        ImGui::TextDisabled("Changes apply on the next shader reload.");

        if (ImGui::Button("Reload shaders"))
            changed = true;

        if (changed)
        {
            if (auto* sceneMgr
                = MWBase::Environment::get().getResourceSystem()->getSceneManager())
                sceneMgr->getShaderManager().triggerShaderReload();
        }

        ImGui::End();
    }
}
