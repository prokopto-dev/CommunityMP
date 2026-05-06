#include "entityinspector.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
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
#include <components/esm/refid.hpp>
#include <components/material/materialdef.hpp>
#include <components/material/materialregistry.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/sceneutil/util.hpp>
#include <components/shader/shadermanager.hpp>
#include <components/terrain/defs.hpp>
#include <components/terrain/storage.hpp>
#include <components/terrain/world.hpp>

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

        // Slot identity used by EntityInspector to re-resolve the UI
        // selection across frames. Pointer-based identity (StateSet*)
        // wouldn't survive a cell reload; nodePath + diffuse is a
        // stable string-based key the slot collector emits anyway.
        std::string slotKeyOf(const MaterialSlot& s)
        {
            return s.mNodePath + "|" + s.mDiffuse;
        }

        // Strip the path prefix on a NIF / texture path so a per-mesh
        // or per-texture rule remains user-friendly even when the
        // engine resolves the asset to a fully-qualified VFS string.
        std::string basenamePath(const std::string& path)
        {
            const auto pos = path.find_last_of("/\\");
            return pos == std::string::npos ? path : path.substr(pos + 1);
        }

        // Same sanitising rule as makeMaterialDefForSlot — kept local
        // here so the inspector can build the on-disk filename and
        // the "<refId>__" prefix used to filter saved defs without
        // depending on materialeditor.cpp internals.
        std::string sanitiseFilename(const std::string& s)
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

    void EntityInspector::onTerrainPick(float normalizedX, float normalizedY)
    {
        mPickTerrainMode = false;
        mTerrainPick = TerrainPick{};

        auto* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
        if (rendering == nullptr)
            return;

        const auto rayRes = rendering->castCameraToViewportRay(
            normalizedX, normalizedY, /*maxDistance*/ 10000.0f, /*ignorePlayer*/ true,
            /*ignoreActors*/ true);
        // Terrain hit: ray reports mHit with no mHitObject (no Ptr).
        // Reject hits that landed on entities — use Pick from world
        // for those.
        if (!rayRes.mHit || !rayRes.mHitObject.isEmpty())
            return;

        // Worldspace from the player's current cell — terrain only
        // makes sense outdoors, and the player is always in some cell
        // when the inspector is reachable.
        const MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.isEmpty() || !player.isInCell())
            return;
        const auto* cellPtr = player.getCell();
        if (cellPtr == nullptr)
            return;
        const auto& cell = *cellPtr->getCell();
        if (!cell.isExterior())
            return;

        const ESM::RefId worldspace = cell.getWorldSpace();

        auto* terrain = rendering->getTerrain();
        auto* storage = terrain ? terrain->getStorage() : nullptr;
        if (storage == nullptr)
            return;

        const float cellSize = storage->getCellWorldSize(worldspace);
        if (cellSize <= 0.0f)
            return;
        const int cellX = static_cast<int>(std::floor(rayRes.mHitPointWorld.x() / cellSize));
        const int cellY = static_cast<int>(std::floor(rayRes.mHitPointWorld.y() / cellSize));

        // getBlendmaps allocates blendmap textures on top of the
        // layer list — overkill for a UI lookup but it's the only
        // public path that exposes the texture set. Cost is bounded
        // by the few-hundred-pixel blendmaps for one cell, fine for
        // an interactive click.
        Terrain::Storage::ImageVector blendmaps;
        std::vector<Terrain::LayerInfo> layers;
        storage->getBlendmaps(/*chunkSize*/ 1.0f,
            osg::Vec2f(static_cast<float>(cellX) + 0.5f, static_cast<float>(cellY) + 0.5f), blendmaps,
            layers, worldspace);

        TerrainPick pick;
        pick.mValid = true;
        std::string ws = worldspace.toDebugString();
        std::transform(ws.begin(), ws.end(), ws.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        pick.mWorldspace = std::move(ws);
        pick.mCellX = cellX;
        pick.mCellY = cellY;
        for (const auto& l : layers)
        {
            pick.mLayerDiffuses.emplace_back(l.mDiffuseMap.value());
            pick.mLayerNormals.emplace_back(l.mNormalMap.value());
            if (l.mParallax)
                pick.mAnyParallax = true;
        }
        mTerrainPick = std::move(pick);
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

            // Phase 8b-quinquies — Material section. Enumerates EVERY
            // distinct StateSet in the entity's subtree (multi-slot),
            // lets the user pick one, then resolves the per-slot
            // override (if any) and renders the editable uniforms
            // inline. Override creation supports four scopes
            // (per-child, per-texture, per-record, per-mesh) which
            // OR together as MatchRules at lookup time.
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
                std::vector<MaterialSlot> slots;
                if (auto* base = mSelected.getRefData().getBaseNode())
                    slots = collectMaterialSlots(*base);

                const std::string refIdStr = cellRef.getRefId().toDebugString();
                const std::string meshBasename = basenamePath(
                    std::string(mSelected.getClass().getModel(mSelected)));

                // Re-resolve the slot selection by string key first,
                // index second. Pointer-based identity wouldn't survive
                // a cell reload, but (nodePath, diffuse) does.
                if (slots.empty())
                {
                    mSelectedSlot = -1;
                    mSelectedSlotKey.clear();
                }
                else
                {
                    int resolved = -1;
                    if (!mSelectedSlotKey.empty())
                    {
                        for (std::size_t i = 0; i < slots.size(); ++i)
                        {
                            if (slotKeyOf(slots[i]) == mSelectedSlotKey)
                            {
                                resolved = static_cast<int>(i);
                                break;
                            }
                        }
                    }
                    if (resolved < 0 && mSelectedSlot >= 0
                        && mSelectedSlot < static_cast<int>(slots.size()))
                        resolved = mSelectedSlot;
                    if (resolved < 0)
                        resolved = 0;
                    mSelectedSlot = resolved;
                    mSelectedSlotKey = slotKeyOf(slots[resolved]);
                }

                // --- Slot table picker ----------------------------------
                if (slots.empty())
                {
                    ImGui::TextDisabled("Material: no textured slots found on this mesh.");
                }
                else
                {
                    ImGui::Text("Material slots: %zu", slots.size());
                    if (ImGui::BeginTable("Slots", 3,
                            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
                                | ImGuiTableFlags_ScrollY,
                            ImVec2(0.0f, 110.0f)))
                    {
                        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 24.0f);
                        ImGui::TableSetupColumn("Node", ImGuiTableColumnFlags_WidthFixed, 140.0f);
                        ImGui::TableSetupColumn("Diffuse");
                        ImGui::TableHeadersRow();
                        for (std::size_t i = 0; i < slots.size(); ++i)
                        {
                            const MaterialSlot& s = slots[i];
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("%zu", i);
                            ImGui::TableNextColumn();
                            const std::string nodeLabel = s.mAnonymous
                                ? std::string("(drawable #" + std::to_string(i) + ")")
                                : s.mNodeName;
                            const bool selected = (static_cast<int>(i) == mSelectedSlot);
                            ImGui::PushID(static_cast<int>(i));
                            if (ImGui::Selectable(nodeLabel.c_str(), selected,
                                    ImGuiSelectableFlags_SpanAllColumns))
                            {
                                mSelectedSlot = static_cast<int>(i);
                                mSelectedSlotKey = slotKeyOf(s);
                            }
                            ImGui::PopID();
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(
                                s.mDiffuse.empty() ? "(no diffuse)" : basenamePath(s.mDiffuse).c_str());
                        }
                        ImGui::EndTable();
                    }
                }

                ImGui::Separator();

                const MaterialSlot* slot = (mSelectedSlot >= 0
                                               && mSelectedSlot < static_cast<int>(slots.size()))
                    ? &slots[mSelectedSlot]
                    : nullptr;

                // recreateShaders runs the ShaderVisitor on every
                // loaded object so newly-added MatchRules / shader
                // prefix swaps actually re-bind to live StateSets —
                // triggerShaderReload alone only recompiles GLSL,
                // which misses match-resolution changes.
                auto recreateAll = [&]() {
                    if (auto* rendering = MWBase::Environment::get().getWorld()->getRenderingManager())
                        if (auto* root = rendering->getObjects().getRootNode())
                            sceneMgr->recreateShaders(root);
                    sceneMgr->getShaderManager().triggerShaderReload();
                };

                if (slot != nullptr)
                {
                    // --- Texture diagnostic block (per slot) ------------
                    ImGui::Text("Diffuse  : %s",
                        slot->mDiffuse.empty() ? "(none)" : slot->mDiffuse.c_str());
                    ImGui::Text("Normal   : %s",
                        slot->mNormal.empty() ? "(none — auto-loading off or no _n.dds)"
                                              : slot->mNormal.c_str());
                    ImGui::Text("Specular : %s",
                        slot->mSpecular.empty() ? "(none — auto-loading off or no _spec.dds)"
                                                : slot->mSpecular.c_str());
                    ImGui::Text("Bump     : %s",
                        slot->mBump.empty() ? "(none — comes from NIF, no auto-detect)"
                                            : slot->mBump.c_str());
                    if (slot->mHasHeightInNormalAlpha)
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                            "Heightmap: present  → parallaxScale will work");
                    else if (!slot->mNormal.empty())
                        ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.3f, 1.0f),
                            "Heightmap: absent   → parallaxScale will be a no-op");
                    if (!slot->mNodePath.empty())
                        ImGui::TextDisabled("Path: %s", slot->mNodePath.c_str());
                    ImGui::Separator();

                    // --- Per-slot override resolution -------------------
                    const Material::MaterialDef* matchedConst = registry->matchMesh(
                        meshBasename, slot->mNodeName, slot->mDiffuse, refIdStr);
                    // Skip terrain-only defs — they shouldn't surface
                    // for entity inspection even if a stray rule keys
                    // happens to align.
                    if (matchedConst != nullptr && matchedConst->mRules.empty()
                        && !matchedConst->mTerrainRules.empty())
                        matchedConst = nullptr;

                    if (matchedConst != nullptr)
                    {
                        ImGui::Text("Material: %s", matchedConst->mName.c_str());
                        ImGui::SameLine();
                        const bool deleteClicked = ImGui::Button("Delete override");
                        Material::MaterialDef* matched
                            = const_cast<Material::MaterialDef*>(matchedConst);
                        if (deleteClicked)
                        {
                            const std::string deletedName = matched->mName;
                            registry->removeByName(deletedName);
                            // Rewrite the entity's YAML with the
                            // surviving overrides; if zero remain,
                            // unlink the file so the next launch
                            // doesn't re-load the deletion away.
                            const std::filesystem::path baseDir
                                = MWBase::Environment::get().getWorld()->getUserDataPath() / "data"
                                / "materials";
                            const std::filesystem::path outPath
                                = baseDir / (sanitiseFilename(refIdStr) + ".yaml");
                            const std::string nameMatchPrefix = sanitiseFilename(refIdStr) + "__";
                            std::vector<const Material::MaterialDef*> survivors;
                            for (std::size_t i = 0; i < registry->size(); ++i)
                            {
                                const auto* def = registry->at(i);
                                if (def == nullptr)
                                    continue;
                                if (def->mName.rfind(nameMatchPrefix, 0) == 0
                                    || def->mName == refIdStr + "_override")
                                    survivors.push_back(def);
                            }
                            if (survivors.empty())
                            {
                                std::error_code ec;
                                std::filesystem::remove(outPath, ec);
                                mLastOverridePath = "deleted: " + deletedName;
                            }
                            else
                            {
                                writeEntityOverrideYaml(outPath, refIdStr, survivors);
                                mLastOverridePath = "rewrote " + outPath.string();
                            }
                            recreateAll();
                        }
                        else if (drawMaterialDefInline(*matched,
                                     ParallaxHint{ slot->mHasHeightInNormalAlpha,
                                         basenamePath(slot->mDiffuse) }))
                        {
                            registry->resort();
                            recreateAll();
                        }
                    }
                    else
                    {
                        // --- Scope picker for fresh override ------------
                        ImGui::TextDisabled("Material: no override matches this slot.");
                        ImGui::Text("Create override scope:");
                        bool perChild = (mPendingScopeFlags & Scope_PerChild) != 0;
                        bool perTexture = (mPendingScopeFlags & Scope_PerTexture) != 0;
                        bool perRecord = (mPendingScopeFlags & Scope_PerRecord) != 0;
                        bool perMesh = (mPendingScopeFlags & Scope_PerMesh) != 0;
                        ImGui::BeginDisabled(slot->mAnonymous);
                        if (ImGui::Checkbox("Per child", &perChild))
                        {
                            mPendingScopeFlags = (mPendingScopeFlags & ~Scope_PerChild)
                                | (perChild ? Scope_PerChild : 0u);
                        }
                        ImGui::EndDisabled();
                        ImGui::SameLine();
                        ImGui::BeginDisabled(slot->mDiffuse.empty());
                        if (ImGui::Checkbox("Per texture", &perTexture))
                        {
                            mPendingScopeFlags = (mPendingScopeFlags & ~Scope_PerTexture)
                                | (perTexture ? Scope_PerTexture : 0u);
                        }
                        ImGui::EndDisabled();
                        ImGui::SameLine();
                        if (ImGui::Checkbox("Per record id", &perRecord))
                        {
                            mPendingScopeFlags = (mPendingScopeFlags & ~Scope_PerRecord)
                                | (perRecord ? Scope_PerRecord : 0u);
                        }
                        ImGui::SameLine();
                        ImGui::BeginDisabled(meshBasename.empty());
                        if (ImGui::Checkbox("Per mesh", &perMesh))
                        {
                            mPendingScopeFlags = (mPendingScopeFlags & ~Scope_PerMesh)
                                | (perMesh ? Scope_PerMesh : 0u);
                        }
                        ImGui::EndDisabled();

                        const bool canCreate = mPendingScopeFlags != 0u;
                        ImGui::BeginDisabled(!canCreate);
                        if (ImGui::Button("Create override"))
                        {
                            // Seed parallaxScale at 0.04 — matches the
                            // engine default (settings-default.cfg
                            // [Shaders] parallax scale) so a fresh
                            // override starts at parity with the rest
                            // of the world; the user can then drag.
                            Material::MaterialDef fresh = makeMaterialDefForSlot(
                                *slot, mPendingScopeFlags, refIdStr, meshBasename, 0.04f);
                            registry->add(std::move(fresh));
                            recreateAll();
                            mLastOverridePath = "created (in-memory) — Save to persist";
                        }
                        ImGui::EndDisabled();
                    }
                }

                // --- Save / status (per entity) -------------------------
                ImGui::Spacing();
                if (ImGui::Button("Save as YAML override"))
                {
                    const std::filesystem::path baseDir
                        = MWBase::Environment::get().getWorld()->getUserDataPath() / "data"
                        / "materials";
                    const std::filesystem::path outPath
                        = baseDir / (sanitiseFilename(refIdStr) + ".yaml");
                    const std::string nameMatchPrefix = sanitiseFilename(refIdStr) + "__";

                    // Collect every registry entry that belongs to
                    // this entity: the new "<sanitisedRefId>__*" defs
                    // built by the scope picker, plus the legacy
                    // "<refId>_override" name still produced by
                    // older saves so a re-save from this UI doesn't
                    // silently lose them.
                    std::vector<const Material::MaterialDef*> defs;
                    for (std::size_t i = 0; i < registry->size(); ++i)
                    {
                        const auto* def = registry->at(i);
                        if (def == nullptr)
                            continue;
                        if (def->mName.rfind(nameMatchPrefix, 0) == 0
                            || def->mName == refIdStr + "_override")
                            defs.push_back(def);
                    }

                    if (defs.empty())
                    {
                        mLastOverridePath = "nothing to save (no override on this entity)";
                    }
                    else if (writeEntityOverrideYaml(outPath, refIdStr, defs))
                    {
                        registry->loadFile(outPath.string());
                        recreateAll();
                        mLastOverridePath = "saved → " + outPath.string();
                    }
                    else
                    {
                        mLastOverridePath = "save failed: " + outPath.string();
                    }
                }

                if (!mLastOverridePath.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("→ %s", mLastOverridePath.c_str());
                }
            }
        }

        // ---------------------------------------------------------------
        // Phase 8b-octies — Terrain section. Visible regardless of entity
        // selection so the user can edit ground material without first
        // picking an object.
        // ---------------------------------------------------------------
        ImGui::Spacing();
        ImGui::Separator();
        if (mPickTerrainMode)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.3f, 0.2f, 1.0f));
            if (ImGui::Button("Cancel terrain pick (Esc)"))
                mPickTerrainMode = false;
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("Click on the ground to inspect a chunk.");
        }
        else
        {
            if (ImGui::Button("Pick terrain"))
            {
                mPickTerrainMode = true;
                mPickMode = false; // mutually exclusive with entity pick
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Then click on the ground.");
        }

        if (mTerrainPick.mValid)
        {
            auto* sceneMgr = MWBase::Environment::get().getResourceSystem()->getSceneManager();
            auto* registry = sceneMgr ? sceneMgr->getMaterialRegistry() : nullptr;

            ImGui::Text(
                "Worldspace: %s   Cell: (%d, %d)", mTerrainPick.mWorldspace.c_str(), mTerrainPick.mCellX,
                mTerrainPick.mCellY);

            if (mTerrainPick.mLayerDiffuses.empty())
            {
                ImGui::TextDisabled("No layer textures resolved at this point.");
            }
            else
            {
                ImGui::TextDisabled("Layers (top → bottom blend):");
                for (std::size_t i = 0; i < mTerrainPick.mLayerDiffuses.size(); ++i)
                {
                    ImGui::BulletText("L%zu  %s", i,
                        mTerrainPick.mLayerDiffuses[i].empty()
                            ? "(no diffuse)"
                            : mTerrainPick.mLayerDiffuses[i].c_str());
                    if (i < mTerrainPick.mLayerNormals.size() && !mTerrainPick.mLayerNormals[i].empty())
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("(n: %s)", mTerrainPick.mLayerNormals[i].c_str());
                    }
                }
                if (mTerrainPick.mAnyParallax)
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                        "At least one layer has heightmap → parallaxScale will work");
                else
                    ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.3f, 1.0f),
                        "No layer carries a heightmap → parallaxScale is a no-op here");
            }

            auto recreateAll = [&]() {
                if (auto* rendering = MWBase::Environment::get().getWorld()->getRenderingManager())
                    if (auto* root = rendering->getObjects().getRootNode())
                        sceneMgr->recreateShaders(root);
                if (sceneMgr)
                    sceneMgr->getShaderManager().triggerShaderReload();
            };

            const Material::MaterialDef* matchedConst = registry
                ? registry->matchTerrain(mTerrainPick.mWorldspace, mTerrainPick.mCellX, mTerrainPick.mCellY)
                : nullptr;

            if (matchedConst != nullptr)
            {
                ImGui::Text("Override: %s", matchedConst->mName.c_str());
                ImGui::SameLine();
                Material::MaterialDef* matched = const_cast<Material::MaterialDef*>(matchedConst);
                if (ImGui::Button("Delete terrain override"))
                {
                    const std::string deletedName = matched->mName;
                    registry->removeByName(deletedName);
                    // Save path: terrain_<worldspace>.yaml — collect
                    // surviving terrain defs for that worldspace and
                    // rewrite, or remove the file when none remain.
                    const std::filesystem::path baseDir
                        = MWBase::Environment::get().getWorld()->getUserDataPath() / "data"
                        / "materials";
                    const std::filesystem::path outPath
                        = baseDir / ("terrain_" + sanitiseFilename(mTerrainPick.mWorldspace) + ".yaml");
                    const std::string prefix = "terrain__" + sanitiseFilename(mTerrainPick.mWorldspace);
                    std::vector<const Material::MaterialDef*> survivors;
                    for (std::size_t i = 0; i < registry->size(); ++i)
                    {
                        const auto* def = registry->at(i);
                        if (def == nullptr)
                            continue;
                        if (def->mName.rfind(prefix, 0) == 0)
                            survivors.push_back(def);
                    }
                    if (survivors.empty())
                    {
                        std::error_code ec;
                        std::filesystem::remove(outPath, ec);
                        mLastOverridePath = "deleted: " + deletedName;
                    }
                    else
                    {
                        writeEntityOverrideYaml(outPath, mTerrainPick.mWorldspace, survivors);
                        mLastOverridePath = "rewrote " + outPath.string();
                    }
                    recreateAll();
                }
                else if (drawMaterialDefInline(*matched, ParallaxHint{ mTerrainPick.mAnyParallax,
                                                          mTerrainPick.mLayerDiffuses.empty()
                                                              ? std::string()
                                                              : basenamePath(mTerrainPick.mLayerDiffuses[0]) }))
                {
                    registry->resort();
                    recreateAll();
                }
            }
            else if (registry != nullptr)
            {
                ImGui::TextDisabled("No terrain override matches this chunk.");
                ImGui::Checkbox("Per worldspace (otherwise per cell)", &mTerrainPerWorldspace);
                if (ImGui::Button("Create terrain override"))
                {
                    Material::MaterialDef fresh = makeTerrainOverride(mTerrainPick.mWorldspace,
                        mTerrainPick.mCellX, mTerrainPick.mCellY, mTerrainPerWorldspace, 0.04f);
                    registry->add(std::move(fresh));
                    recreateAll();
                    mLastOverridePath = "created (in-memory) — Save to persist";
                }
            }

            ImGui::Spacing();
            if (registry != nullptr && ImGui::Button("Save terrain override"))
            {
                const std::filesystem::path baseDir
                    = MWBase::Environment::get().getWorld()->getUserDataPath() / "data" / "materials";
                const std::filesystem::path outPath
                    = baseDir / ("terrain_" + sanitiseFilename(mTerrainPick.mWorldspace) + ".yaml");
                const std::string prefix = "terrain__" + sanitiseFilename(mTerrainPick.mWorldspace);

                std::vector<const Material::MaterialDef*> defs;
                for (std::size_t i = 0; i < registry->size(); ++i)
                {
                    const auto* def = registry->at(i);
                    if (def == nullptr)
                        continue;
                    if (def->mName.rfind(prefix, 0) == 0)
                        defs.push_back(def);
                }
                if (defs.empty())
                {
                    mLastOverridePath = "nothing to save (no terrain override on this worldspace)";
                }
                else if (writeEntityOverrideYaml(outPath, mTerrainPick.mWorldspace, defs))
                {
                    registry->loadFile(outPath.string());
                    recreateAll();
                    mLastOverridePath = "saved → " + outPath.string();
                }
                else
                {
                    mLastOverridePath = "save failed: " + outPath.string();
                }
            }
        }

        ImGui::EndChild();

        ImGui::End();
    }
}
