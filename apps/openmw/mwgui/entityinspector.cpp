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

        // Phase 8b-bis — walk a Ptr's BaseNode subtree to grab the
        // first diffuse texture filename and a meaningful node name.
        // Used to query Material::Registry for matching overrides
        // from the EntityInspector. Stops at the first hit; multi-
        // material meshes only get their first sub-material exposed.
        struct DiffuseProbe : public osg::NodeVisitor
        {
            DiffuseProbe()
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
            {
            }

            std::string mDiffuse;
            std::string mNodeName;

            void apply(osg::Node& node) override
            {
                if (mDiffuse.empty() && node.getStateSet())
                    inspect(*node.getStateSet(), node.getName());
                if (mDiffuse.empty())
                    traverse(node);
            }

            void apply(osg::Drawable& drawable) override
            {
                if (mDiffuse.empty() && drawable.getStateSet())
                    inspect(*drawable.getStateSet(), drawable.getName());
            }

            void inspect(const osg::StateSet& ss, const std::string& candidateName)
            {
                // Only the diffuse map (TU0) is relevant for matching;
                // OpenMW's shader visitor uses the same convention.
                const osg::Texture* tex
                    = dynamic_cast<const osg::Texture*>(ss.getTextureAttribute(0, osg::StateAttribute::TEXTURE));
                if (tex == nullptr || tex->getImage(0) == nullptr)
                    return;
                mDiffuse = tex->getImage(0)->getFileName();
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
                std::string diffuse;
                std::string nodeName;
                if (auto* base = mSelected.getRefData().getBaseNode())
                {
                    DiffuseProbe probe;
                    base->accept(probe);
                    diffuse = std::move(probe.mDiffuse);
                    nodeName = std::move(probe.mNodeName);
                }
                const std::string refIdStr = cellRef.getRefId().toDebugString();
                const Material::MaterialDef* matched
                    = registry->matchMesh(/*meshPath=*/"", nodeName, diffuse, refIdStr);
                if (matched == nullptr)
                {
                    ImGui::TextDisabled("Material: no override matches this object.");
                    if (!diffuse.empty())
                        ImGui::TextDisabled("(diffuse: %s)", diffuse.c_str());
                }
                else
                {
                    ImGui::Text("Material: %s", matched->mName.c_str());
                    // matchMesh returned a const pointer but the
                    // registry's storage is mutable — cast through to
                    // expose the editable uniforms (no other observer
                    // mutates the def at runtime).
                    if (drawMaterialDefInline(*const_cast<Material::MaterialDef*>(matched)))
                        sceneMgr->getShaderManager().triggerShaderReload();
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
                    {
                        std::ofstream out(outPath);
                        out << "# Generated by EntityInspector → Save as YAML override.\n";
                        out << "# Tweak in-place; click 'Reload from disk' in the\n";
                        out << "# Materials window or the inspector to re-pick changes.\n";
                        out << "name: " << refIdStr << "_override\n";
                        out << "priority: 100\n";
                        out << "match:\n";
                        out << "  any:\n";
                        out << "    - record_id: " << refIdStr << "\n";
                        if (!diffuse.empty())
                            out << "# diffuse hint: " << diffuse << "\n";
                        if (matched != nullptr && !matched->mShaderPrefix.empty())
                            out << "shader: " << matched->mShaderPrefix << "\n";
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
                    }
                    Log(Debug::Info) << "[material] wrote override to " << outPath.string();
                    registry->reload(MWBase::Environment::get().getResourceSystem()->getVFS());
                    sceneMgr->getShaderManager().triggerShaderReload();
                    mLastOverridePath = outPath.string();
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
