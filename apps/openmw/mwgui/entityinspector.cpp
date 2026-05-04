#include "entityinspector.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

#include <filesystem>
#include <fstream>

#include <imgui.h>

#include <osg/Geode>
#include <osg/Image>
#include <osg/NodeVisitor>
#include <osg/StateSet>
#include <osg/Texture>

#include <components/debug/debuglog.hpp>
#include <components/vfs/manager.hpp>

#include <components/esm/defs.hpp>
#include <components/esm3/cellref.hpp>
#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/material/materialdef.hpp>
#include <components/material/materialregistry.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/shader/shadermanager.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwmechanics/actorutil.hpp"
#include "../mwphysics/raycasting.hpp"
#include "../mwrender/renderingmanager.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/scene.hpp"

#include "materialeditor.hpp"

namespace MWGui
{
    namespace
    {
        const char* typeShortName(unsigned int type)
        {
            switch (type)
            {
                case ESM::REC_STAT: return "Static";
                case ESM::REC_ACTI: return "Activator";
                case ESM::REC_DOOR: return "Door";
                case ESM::REC_NPC_: return "NPC";
                case ESM::REC_CREA: return "Creature";
                case ESM::REC_CONT: return "Container";
                case ESM::REC_LIGH: return "Light";
                case ESM::REC_MISC: return "Misc";
                default: return "Other";
            }
        }

        // Phase 8b-bis — walk a Ptr's BaseNode subtree, capture the
        // first sub-material's full texture set + node name. The
        // EntityInspector uses this both to query Material::Registry
        // for matching overrides AND to surface parallax / normal /
        // specular state so the user can tell at a glance whether
        // a parallaxScale uniform will actually do anything (it
        // only fires when a heightmap exists, which OpenMW autoloads
        // from the normal map's alpha channel or via the normal-
        // height pattern e.g. `_nh.dds`).
        struct MaterialProbe : public osg::NodeVisitor
        {
            MaterialProbe()
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
            {
            }

            std::string mDiffuse;
            std::string mNormal;
            std::string mSpecular;
            std::string mNodeName;
            bool mHasHeightInNormalAlpha = false;
            bool mFound = false;

            void apply(osg::Node& node) override
            {
                if (!mFound && node.getStateSet())
                    inspect(*node.getStateSet(), node.getName());
                if (!mFound)
                    traverse(node);
            }

            void apply(osg::Drawable& drawable) override
            {
                if (!mFound && drawable.getStateSet())
                    inspect(*drawable.getStateSet(), drawable.getName());
            }

            void inspect(const osg::StateSet& ss, const std::string& candidateName)
            {
                auto getFn = [&](unsigned int unit) -> std::string {
                    const osg::Texture* tex = dynamic_cast<const osg::Texture*>(
                        ss.getTextureAttribute(unit, osg::StateAttribute::TEXTURE));
                    if (tex == nullptr || tex->getImage(0) == nullptr)
                        return {};
                    return tex->getImage(0)->getFileName();
                };
                const std::string diffuse = getFn(0);
                if (diffuse.empty())
                    return;
                mFound = true;
                mDiffuse = diffuse;
                // OpenMW shader visitor binds normal map at TU1 and
                // specular at TU2 once auto-detection runs (cf
                // ShaderVisitor::createProgram); inspect both.
                mNormal = getFn(1);
                if (mNormal.empty())
                    mNormal = getFn(2);
                mSpecular = getFn(3);
                if (!mNormal.empty())
                {
                    // Heuristic for parallax-eligibility: the normal
                    // map carries a height channel either via the
                    // engine's `*_nh.dds` pattern or via an actual
                    // RGBA image (alpha == height). Both are what
                    // ShaderVisitor flips on for `parallax = 1`.
                    const osg::Texture* nrm = dynamic_cast<const osg::Texture*>(
                        ss.getTextureAttribute(1, osg::StateAttribute::TEXTURE));
                    if (nrm == nullptr)
                        nrm = dynamic_cast<const osg::Texture*>(
                            ss.getTextureAttribute(2, osg::StateAttribute::TEXTURE));
                    if (nrm != nullptr && nrm->getImage(0) != nullptr)
                    {
                        const auto* img = nrm->getImage(0);
                        const GLenum fmt = img->getPixelFormat();
                        mHasHeightInNormalAlpha = (fmt == GL_RGBA || fmt == GL_BGRA);
                    }
                    if (!mHasHeightInNormalAlpha)
                    {
                        const std::string lower = [&]() {
                            std::string s = mNormal;
                            std::transform(s.begin(), s.end(), s.begin(),
                                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                            return s;
                        }();
                        // Default normal-height pattern (Settings: shaders mNormalHeightMapPattern, "_nh").
                        if (lower.find("_nh.") != std::string::npos
                            || lower.find("_nh_") != std::string::npos)
                            mHasHeightInNormalAlpha = true;
                    }
                }
                if (mNodeName.empty())
                    mNodeName = candidateName;
            }
        };
    }

    EntityInspector::EntityInspector() = default;

    void EntityInspector::onWorldPick(float normalizedX, float normalizedY)
    {
        mPickMode = false;
        auto* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
        if (rendering == nullptr)
            return;
        const auto rayRes = rendering->castCameraToViewportRay(
            normalizedX, normalizedY, /*maxDistance*/ 5000.0f, /*ignorePlayer*/ true,
            /*ignoreActors*/ false);
        if (rayRes.mHit && !rayRes.mHitObject.isEmpty())
            mSelected = rayRes.mHitObject;
    }

    void EntityInspector::draw()
    {
        ImGui::SetNextWindowSize(ImVec2(720.0f, 540.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Entity Inspector"))
        {
            ImGui::End();
            return;
        }

        const MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.isEmpty() || !player.isInCell())
        {
            ImGui::TextDisabled("No player in world.");
            ImGui::End();
            return;
        }

        const osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();

        // --- Pick mode toggle ----------------------------------------
        if (mPickMode)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.3f, 0.2f, 1.0f));
            if (ImGui::Button("Cancel pick (Esc)"))
                mPickMode = false;
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("Click in the world to select an object.");
        }
        else
        {
            if (ImGui::Button("Pick from world"))
                mPickMode = true;
            ImGui::SameLine();
            ImGui::TextDisabled("Then click in the scene to select.");
        }
        ImGui::Separator();

        // --- Filters --------------------------------------------------
        ImGui::Text("Filters");
        ImGui::Separator();
        ImGui::InputTextWithHint("RefId", "substring", mNameFilter, sizeof(mNameFilter));
        ImGui::SliderFloat("Max distance", &mMaxDistance, 100.0f, 20000.0f, "%.0f");
        ImGui::Columns(3, nullptr, false);
        ImGui::Checkbox("Static", &mShowStatic);
        ImGui::Checkbox("Activator", &mShowActivator);
        ImGui::Checkbox("Door", &mShowDoor);
        ImGui::NextColumn();
        ImGui::Checkbox("NPC", &mShowNpc);
        ImGui::Checkbox("Creature", &mShowCreature);
        ImGui::Checkbox("Container", &mShowContainer);
        ImGui::NextColumn();
        ImGui::Checkbox("Light", &mShowLight);
        ImGui::Checkbox("Misc", &mShowMisc);
        ImGui::Checkbox("Other", &mShowOther);
        ImGui::Columns(1);
        ImGui::Separator();

        // --- Collect ptrs from active cells ---------------------------
        struct Entry
        {
            MWWorld::Ptr ptr;
            float distance;
        };
        std::vector<Entry> entries;
        entries.reserve(256);

        const auto& activeCells = MWBase::Environment::get().getWorldScene()->getActiveCells();
        std::string filterLower;
        if (mNameFilter[0] != '\0')
        {
            filterLower = mNameFilter;
            std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        }

        const float maxDistSq = mMaxDistance * mMaxDistance;

        for (MWWorld::CellStore* cell : activeCells)
        {
            cell->forEach([&](const MWWorld::Ptr& ptr) -> bool {
                const unsigned int type = ptr.getType();
                bool keep = false;
                switch (type)
                {
                    case ESM::REC_STAT: keep = mShowStatic; break;
                    case ESM::REC_ACTI: keep = mShowActivator; break;
                    case ESM::REC_DOOR: keep = mShowDoor; break;
                    case ESM::REC_NPC_: keep = mShowNpc; break;
                    case ESM::REC_CREA: keep = mShowCreature; break;
                    case ESM::REC_CONT: keep = mShowContainer; break;
                    case ESM::REC_LIGH: keep = mShowLight; break;
                    case ESM::REC_MISC: keep = mShowMisc; break;
                    default: keep = mShowOther; break;
                }
                if (!keep)
                    return true;

                const osg::Vec3f pos = ptr.getRefData().getPosition().asVec3();
                const float distSq = (pos - playerPos).length2();
                if (distSq > maxDistSq)
                    return true;

                if (!filterLower.empty())
                {
                    std::string refIdLower = ptr.getCellRef().getRefId().toDebugString();
                    std::transform(refIdLower.begin(), refIdLower.end(), refIdLower.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (refIdLower.find(filterLower) == std::string::npos)
                        return true;
                }

                entries.push_back({ ptr, std::sqrt(distSq) });
                return true;
            });
        }

        std::sort(entries.begin(), entries.end(),
            [](const Entry& a, const Entry& b) { return a.distance < b.distance; });

        // Re-validate selection against the rebuilt snapshot. A Ptr can
        // dangle if its cell got unloaded or it was deleted between
        // frames — comparing identity to live entries is the cheapest
        // way to detect that without poking RefData internals.
        bool selectionStillLive = false;
        if (!mSelected.isEmpty())
        {
            for (const Entry& e : entries)
            {
                if (e.ptr == mSelected)
                {
                    selectionStillLive = true;
                    break;
                }
            }
        }
        if (!selectionStillLive)
            mSelected = MWWorld::Ptr();

        // --- Two-pane layout: list on the left, details on the right --
        const float listWidth = ImGui::GetContentRegionAvail().x * 0.55f;

        ImGui::BeginChild("EntityList", ImVec2(listWidth, 0.0f), ImGuiChildFlags_Borders);
        ImGui::Text("%zu / %zu visible", entries.size(),
            entries.size()); // placeholder — total count would need a second pass
        if (ImGui::BeginTable("Entities", 3,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY))
        {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("RefId");
            ImGui::TableHeadersRow();
            for (const Entry& e : entries)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(typeShortName(e.ptr.getType()));
                ImGui::TableNextColumn();
                ImGui::Text("%.0f", e.distance);
                ImGui::TableNextColumn();
                const std::string refId = e.ptr.getCellRef().getRefId().toDebugString();
                const bool selected = (mSelected == e.ptr);
                ImGui::PushID(refId.c_str());
                if (ImGui::Selectable(refId.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
                    mSelected = e.ptr;
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("EntityDetails", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
        if (mSelected.isEmpty())
        {
            ImGui::TextDisabled("Select an entity in the list.");
        }
        else
        {
            const auto& cellRef = mSelected.getCellRef();
            const std::string cellName{ mSelected.getCell()->getCell()->getDescription() };

            ImGui::Text("RefId : %s", cellRef.getRefId().toDebugString().c_str());
            ImGui::Text("Type  : %s", typeShortName(mSelected.getType()));
            ImGui::Text("Cell  : %s", cellName.c_str());
            const std::string name = std::string(mSelected.getClass().getName(mSelected));
            if (!name.empty())
                ImGui::Text("Name  : %s", name.c_str());
            ImGui::Text("Count : %d", cellRef.getCount());
            ImGui::Separator();

            // Re-read live transform from RefData every frame so
            // physics-driven motion (e.g. dropped items, NPCs) shows
            // up immediately, and so widget values reflect the actual
            // post-mutation state when we edit.
            const ESM::Position& curPos = mSelected.getRefData().getPosition();

            float posArr[3] = { curPos.pos[0], curPos.pos[1], curPos.pos[2] };
            if (ImGui::DragFloat3("Position", posArr, 1.0f, -1.0e6f, 1.0e6f, "%.2f"))
            {
                const osg::Vec3f newPos(posArr[0], posArr[1], posArr[2]);
                // moveObject can change the Ptr's cell — keep our
                // selection in sync with the returned Ptr or it would
                // dangle on cell transitions.
                mSelected = MWBase::Environment::get().getWorld()->moveObject(
                    mSelected, newPos, /*movePhysics*/ true, /*moveToActive*/ false);
            }

            float rotDeg[3] = {
                osg::RadiansToDegrees(curPos.rot[0]),
                osg::RadiansToDegrees(curPos.rot[1]),
                osg::RadiansToDegrees(curPos.rot[2]),
            };
            if (ImGui::DragFloat3("Rotation", rotDeg, 0.5f, -180.0f, 180.0f, "%.1f deg"))
            {
                const osg::Vec3f rotRad(osg::DegreesToRadians(rotDeg[0]),
                    osg::DegreesToRadians(rotDeg[1]), osg::DegreesToRadians(rotDeg[2]));
                MWBase::Environment::get().getWorld()->rotateObject(
                    mSelected, rotRad, MWBase::RotationFlag_inverseOrder);
            }

            float scale = cellRef.getScale();
            if (ImGui::DragFloat("Scale", &scale, 0.01f, 0.1f, 10.0f, "%.2f"))
                MWBase::Environment::get().getWorld()->scaleObject(mSelected, scale);

            ImGui::Spacing();
            ImGui::TextDisabled("Drag the values, or Ctrl+click to type.");

            // Phase 8b-bis — Material section. Probes the BaseNode for
            // a diffuse texture, queries the registry for any rule
            // matching by refId / texture / node name, and renders the
            // editable uniforms inline. Triggers shader reload on edit.
            ImGui::Spacing();
            ImGui::Separator();
            auto* sceneMgr = MWBase::Environment::get().getResourceSystem()->getSceneManager();
            auto* registry = sceneMgr ? sceneMgr->getMaterialRegistry() : nullptr;
            if (registry == nullptr)
            {
                ImGui::TextDisabled("Material registry not available.");
            }
            else
            {
                MaterialProbe probe;
                if (auto* base = mSelected.getRefData().getBaseNode())
                    base->accept(probe);
                const std::string& diffuse = probe.mDiffuse;
                const std::string& nodeName = probe.mNodeName;
                const std::string refIdStr = cellRef.getRefId().toDebugString();

                // --- Texture diagnostic block ---------------------------
                if (diffuse.empty())
                {
                    ImGui::TextDisabled("Material: no diffuse texture found on this mesh.");
                }
                else
                {
                    ImGui::Text("Diffuse  : %s", diffuse.c_str());
                    ImGui::Text("Normal   : %s",
                        probe.mNormal.empty() ? "(none — auto-loading off or no _n.dds)"
                                              : probe.mNormal.c_str());
                    ImGui::Text("Specular : %s",
                        probe.mSpecular.empty() ? "(none)" : probe.mSpecular.c_str());
                    if (probe.mHasHeightInNormalAlpha)
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                            "Heightmap: present  → parallaxScale will work");
                    else
                        ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.3f, 1.0f),
                            "Heightmap: absent   → parallaxScale will be a no-op");
                    if (!nodeName.empty())
                        ImGui::TextDisabled("Node: %s", nodeName.c_str());
                }
                ImGui::Separator();

                const Material::MaterialDef* matched
                    = registry->matchMesh(/*meshPath=*/"", nodeName, diffuse, refIdStr);
                if (matched == nullptr)
                {
                    ImGui::TextDisabled("Material: no override matches this object.");
                }
                else
                {
                    ImGui::Text("Material: %s", matched->mName.c_str());
                    ImGui::SameLine();
                    if (ImGui::Button("Delete override"))
                    {
                        // Drop the in-memory entry. If the YAML lives
                        // under <userdata>/data/materials/, also unlink
                        // it on disk so the next launch doesn't reload
                        // the deletion away.
                        const std::string name = matched->mName;
                        const std::filesystem::path baseDir
                            = MWBase::Environment::get().getWorld()->getUserDataPath()
                            / "data" / "materials";
                        // The save path filename mirrors the refId
                        // (sanitised) — we try both conventions.
                        std::string sanitised = refIdStr;
                        for (char& c : sanitised)
                            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_'
                                && c != '.' && c != '-')
                                c = '_';
                        std::error_code ec;
                        std::filesystem::remove(baseDir / (sanitised + ".yaml"), ec);
                        registry->removeByName(name);
                        sceneMgr->getShaderManager().triggerShaderReload();
                        mLastOverridePath = "deleted: " + name;
                    }
                    // matchMesh returned a const pointer but the
                    // registry's storage is mutable — cast through to
                    // expose the editable uniforms. resort() handles
                    // the priority-change case so the next matchMesh
                    // honours the new ordering.
                    else if (drawMaterialDefInline(*const_cast<Material::MaterialDef*>(matched)))
                    {
                        registry->resort();
                        sceneMgr->getShaderManager().triggerShaderReload();
                    }
                }

                // Phase 8b-bis — write a YAML override targeting the
                // current RefId. Lands in <userdata>/materials/ which
                // the VFS picks up, then we reload the registry +
                // trigger a shader rebuild so it appears live.
                ImGui::Spacing();
                if (ImGui::Button("Save as YAML override"))
                {
                    // Land in <userdata>/data/materials/ — the
                    // <userdata>/data subdir is on most OpenMW
                    // installs' default data path list, so the file
                    // gets picked up by the VFS at reload without
                    // any cfg edit required.
                    const std::filesystem::path baseDir
                        = MWBase::Environment::get().getWorld()->getUserDataPath() / "data"
                        / "materials";
                    std::error_code ec;
                    std::filesystem::create_directories(baseDir, ec);
                    // Sanitise the RefId for use as a filename: only
                    // [a-zA-Z0-9_.-] survive, everything else → '_'.
                    std::string filename = refIdStr;
                    for (char& c : filename)
                    {
                        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.' && c != '-')
                            c = '_';
                    }
                    filename += ".yaml";
                    const std::filesystem::path outPath = baseDir / filename;
                    if (ec)
                    {
                        mLastOverridePath = "mkdir failed: " + ec.message() + " (" + baseDir.string() + ")";
                        Log(Debug::Warning) << "[material] " << mLastOverridePath;
                    }
                    else
                    {
                        std::ofstream out(outPath);
                        if (!out)
                        {
                            mLastOverridePath = "open failed: " + outPath.string();
                            Log(Debug::Warning) << "[material] " << mLastOverridePath;
                        }
                        else
                        {
                            // Single-quoted YAML scalar so refIds
                            // containing '\', ':', etc. parse cleanly.
                            // Escape literal ' by doubling it.
                            auto yamlQuote = [](const std::string& s) {
                                std::string out2 = "'";
                                for (char c : s)
                                {
                                    if (c == '\'')
                                        out2 += "''";
                                    else
                                        out2 += c;
                                }
                                out2 += '\'';
                                return out2;
                            };

                            out << "# Generated by EntityInspector → Save as YAML override.\n";
                            out << "# Texture audit at save time:\n";
                            out << "#   diffuse  : " << (diffuse.empty() ? "(none)" : diffuse) << "\n";
                            out << "#   normal   : " << (probe.mNormal.empty() ? "(none)" : probe.mNormal)
                                << "\n";
                            out << "#   specular : "
                                << (probe.mSpecular.empty() ? "(none)" : probe.mSpecular) << "\n";
                            out << "#   heightmap: "
                                << (probe.mHasHeightInNormalAlpha ? "yes (parallaxScale will work)"
                                                                  : "NO (parallaxScale = no-op)")
                                << "\n";
                            out << "name: " << yamlQuote(refIdStr + "_override") << "\n";
                            out << "priority: 100\n";
                            out << "match:\n";
                            out << "  any:\n";
                            out << "    - record_id: " << yamlQuote(refIdStr) << "\n";
                            if (matched != nullptr && !matched->mShaderPrefix.empty())
                                out << "shader: " << yamlQuote(matched->mShaderPrefix) << "\n";
                            out << "uniforms:\n";
                            if (matched != nullptr && !matched->mUniforms.empty())
                            {
                                // Write current values verbatim so the
                                // saved YAML mirrors the live state.
                                for (const auto& u : matched->mUniforms)
                                {
                                    out << "  - { name: " << u.mName << ", type: float, value: ";
                                    if (auto* f = std::get_if<float>(&u.mValue))
                                        out << *f;
                                    else
                                        out << 0.0f;
                                    out << " }\n";
                                }
                            }
                            else
                            {
                                out << "  - { name: parallaxScale, type: float, value: 0.04 }\n";
                            }
                            out.close();
                            Log(Debug::Info) << "[material] wrote override to " << outPath.string();
                            const bool loaded = registry->loadFile(outPath.string());
                            sceneMgr->getShaderManager().triggerShaderReload();
                            mLastOverridePath
                                = (loaded ? "saved → " : "saved (parse failed) → ") + outPath.string();
                        }
                    }
                }
                if (!mLastOverridePath.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("→ %s", mLastOverridePath.c_str());
                }
            }
        }
        ImGui::EndChild();

        ImGui::End();
    }
}
