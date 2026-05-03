#include "objectspawner.hpp"

#include <algorithm>
#include <cstring>

#include <imgui.h>

#include <osg/Vec3f>

#include <components/esm/defs.hpp>
#include <components/esm/exteriorcelllocation.hpp>
#include <components/esm/util.hpp>
#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadstat.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwmechanics/actorutil.hpp"
#include "../mwphysics/iphysicsbackend.hpp"
#include "../mwphysics/raycasting.hpp"
#include "../mwrender/renderingmanager.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/manualref.hpp"
#include "../mwworld/refdata.hpp"
#include "../mwworld/worldmodel.hpp"

namespace MWGui
{
    namespace
    {
        // Run a callback over every (refId, displayName) pair for the
        // record type backing the catalog tab. Templated so each type
        // gets its own concrete instantiation rather than a runtime
        // visit, since ESMStore's per-type Stores aren't unified
        // behind a base iterator we could use here.
        template <typename T, typename F>
        void forEachRecord(F&& f)
        {
            const auto& store = MWBase::Environment::get().getESMStore()->get<T>();
            for (auto it = store.begin(); it != store.end(); ++it)
                f(it->mId);
        }

        // Resolve the cell that should host a spawn at world coords
        // (x, y, z). Mirrors the OpPlaceItem path in
        // mwscript/transformationextensions.cpp:567-575.
        MWWorld::CellStore* resolveCell(float x, float y)
        {
            const MWWorld::Ptr player = MWMechanics::getPlayer();
            if (!player.isInCell())
                return nullptr;
            if (player.getCell()->isExterior())
            {
                const auto idx = ESM::positionToExteriorCellLocation(
                    x, y, player.getCell()->getCell()->getWorldSpace());
                return &MWBase::Environment::get().getWorldModel()->getExterior(idx);
            }
            return player.getCell();
        }
    }

    void ObjectSpawner::draw()
    {
        ImGui::SetNextWindowSize(ImVec2(560.0f, 520.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Object Spawner"))
        {
            ImGui::End();
            return;
        }

        // --- Type tabs ----------------------------------------------
        if (ImGui::BeginTabBar("SpawnerTypes"))
        {
            const auto tab = [&](const char* name, Type t) {
                if (ImGui::BeginTabItem(name))
                {
                    if (mType != t)
                    {
                        mType = t;
                        mSelected = ESM::RefId();
                    }
                    ImGui::EndTabItem();
                }
            };
            tab("Static", Type::Static);
            tab("Activator", Type::Activator);
            tab("Container", Type::Container);
            tab("Light", Type::Light);
            tab("Misc", Type::Misc);
            tab("Door", Type::Door);
            ImGui::EndTabBar();
        }

        // --- Search + catalog ---------------------------------------
        ImGui::InputTextWithHint("Search", "RefId substring", mSearchFilter, sizeof(mSearchFilter));
        ImGui::BeginChild("Catalog", ImVec2(0.0f, 220.0f), ImGuiChildFlags_Borders);
        renderCatalog();
        ImGui::EndChild();

        // --- Placement ----------------------------------------------
        ImGui::Separator();
        ImGui::Text("Placement");
        ImGui::RadioButton("At player", reinterpret_cast<int*>(&mPlacement),
            static_cast<int>(PlacementMode::AtPlayer));
        ImGui::SameLine();
        ImGui::RadioButton("At crosshair", reinterpret_cast<int*>(&mPlacement),
            static_cast<int>(PlacementMode::AtCrosshair));
        ImGui::SameLine();
        ImGui::RadioButton("At coords", reinterpret_cast<int*>(&mPlacement),
            static_cast<int>(PlacementMode::AtCoords));

        if (mPlacement == PlacementMode::AtCoords)
            ImGui::DragFloat3("Coords", mCoords, 1.0f, -1.0e6f, 1.0e6f, "%.1f");

        ImGui::DragFloat("Z rotation", &mZRotDeg, 1.0f, -180.0f, 180.0f, "%.1f deg");
        ImGui::SliderInt("Count", &mCount, 1, 20);

        // --- Dynamic body promotion (Phase 6) -----------------------
        ImGui::Separator();
        ImGui::Checkbox("Spawn as dynamic (Jolt rigid body)", &mDynamic);
        if (mDynamic)
        {
            const char* shapes[] = { "Box", "Cylinder", "Sphere", "Mesh (auto convex hull)" };
            int shapeIdx = static_cast<int>(mDynamicShape);
            if (ImGui::Combo("Shape", &shapeIdx, shapes, IM_ARRAYSIZE(shapes)))
                mDynamicShape = static_cast<DynamicShape>(shapeIdx);
            if (mDynamicShape != DynamicShape::Mesh)
                ImGui::DragFloat3("Half extents", mDynamicHalfExtents, 1.0f, 1.0f, 256.0f, "%.0f");
            ImGui::DragFloat("Mass", &mDynamicMass, 0.5f, 0.1f, 500.0f, "%.1f kg");
        }

        // --- Spawn button + status ----------------------------------
        ImGui::Separator();
        const bool ready = !mSelected.empty();
        if (!ready)
            ImGui::BeginDisabled();
        if (ImGui::Button("Spawn", ImVec2(140.0f, 0.0f)))
            doSpawn();
        if (!ready)
            ImGui::EndDisabled();

        if (!mLastResult.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", mLastResult.c_str());
        }

        ImGui::End();
    }

    void ObjectSpawner::renderCatalog()
    {
        std::string filterLower;
        if (mSearchFilter[0] != '\0')
        {
            filterLower = mSearchFilter;
            std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        }

        auto renderRow = [&](const ESM::RefId& id) {
            const std::string idStr = id.toDebugString();
            if (!filterLower.empty())
            {
                std::string lower = idStr;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (lower.find(filterLower) == std::string::npos)
                    return;
            }
            const bool selected = (mSelected == id);
            if (ImGui::Selectable(idStr.c_str(), selected))
                mSelected = id;
        };

        switch (mType)
        {
            case Type::Static:    forEachRecord<ESM::Static>(renderRow); break;
            case Type::Activator: forEachRecord<ESM::Activator>(renderRow); break;
            case Type::Container: forEachRecord<ESM::Container>(renderRow); break;
            case Type::Light:     forEachRecord<ESM::Light>(renderRow); break;
            case Type::Misc:      forEachRecord<ESM::Miscellaneous>(renderRow); break;
            case Type::Door:      forEachRecord<ESM::Door>(renderRow); break;
        }
    }

    void ObjectSpawner::doSpawn()
    {
        if (mSelected.empty())
            return;

        // Resolve target world position by mode.
        osg::Vec3f worldPos(0.0f, 0.0f, 0.0f);
        switch (mPlacement)
        {
            case PlacementMode::AtPlayer:
            {
                const MWWorld::Ptr player = MWMechanics::getPlayer();
                if (!player.isInCell())
                {
                    mLastResult = "no player in cell";
                    return;
                }
                worldPos = player.getRefData().getPosition().asVec3();
                break;
            }
            case PlacementMode::AtCrosshair:
            {
                auto* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
                if (rendering == nullptr)
                {
                    mLastResult = "no rendering manager";
                    return;
                }
                const auto rayRes
                    = rendering->castCameraToViewportRay(0.5f, 0.5f, 5000.0f, true, true);
                if (!rayRes.mHit)
                {
                    mLastResult = "crosshair ray missed";
                    return;
                }
                // Spawn 1 metre (~70 MW units) above the hit point
                // so the object can settle from a small drop instead
                // of starting clipped into the surface — covers
                // sloped terrain and cluttered tabletops alike.
                worldPos = rayRes.mHitPointWorld;
                worldPos.z() += 70.0f;
                break;
            }
            case PlacementMode::AtCoords:
                worldPos.set(mCoords[0], mCoords[1], mCoords[2]);
                break;
        }

        MWWorld::CellStore* cell = resolveCell(worldPos.x(), worldPos.y());
        if (cell == nullptr)
        {
            mLastResult = "no host cell";
            return;
        }

        // Mirror the OpPlaceItem console path so persisted refs and
        // physics postponement stay consistent with vanilla behaviour
        // (mwscript/transformationextensions.cpp:582-588).
        ESM::Position pos;
        pos.pos[0] = worldPos.x();
        pos.pos[1] = worldPos.y();
        pos.pos[2] = worldPos.z();
        pos.rot[0] = pos.rot[1] = 0.0f;
        pos.rot[2] = osg::DegreesToRadians(mZRotDeg);

        try
        {
            MWWorld::ManualRef ref(*MWBase::Environment::get().getESMStore(), mSelected, mCount);
            ref.getPtr().mRef->mData.mPhysicsPostponed = !ref.getPtr().getClass().isActor();
            ref.getPtr().getCellRef().setPosition(pos);
            MWWorld::Ptr placed
                = MWBase::Environment::get().getWorld()->placeObject(ref.getPtr(), cell, pos);
            placed.getClass().adjustPosition(placed, true);

            if (mDynamic)
            {
                if (auto* phys = MWBase::Environment::get().getWorld()->getPhysicsBackend())
                {
                    using Shape = MWPhysics::IPhysicsBackend::DynamicShape;
                    Shape backendShape = Shape::Box;
                    switch (mDynamicShape)
                    {
                        case DynamicShape::Box: backendShape = Shape::Box; break;
                        case DynamicShape::Cylinder: backendShape = Shape::Cylinder; break;
                        case DynamicShape::Sphere: backendShape = Shape::Sphere; break;
                        case DynamicShape::Mesh: backendShape = Shape::Mesh; break;
                    }
                    phys->promoteToDynamic(placed, backendShape,
                        osg::Vec3f(mDynamicHalfExtents[0], mDynamicHalfExtents[1], mDynamicHalfExtents[2]),
                        mDynamicMass);

                    // Stamp the dynamic record onto RefData so the
                    // savegame keeps it dynamic on reload (Phase 6c).
                    // Rotation seed = identity; the per-frame sync in
                    // JoltPhysicsSystem::stepSimulation will overwrite
                    // it as soon as the body integrates.
                    ESM::DynamicBodyState dbs;
                    dbs.mShape = static_cast<uint8_t>(backendShape);
                    dbs.mHalfExtents[0] = mDynamicHalfExtents[0];
                    dbs.mHalfExtents[1] = mDynamicHalfExtents[1];
                    dbs.mHalfExtents[2] = mDynamicHalfExtents[2];
                    dbs.mMass = mDynamicMass;
                    dbs.mRotation[0] = 0.0f;
                    dbs.mRotation[1] = 0.0f;
                    dbs.mRotation[2] = 0.0f;
                    dbs.mRotation[3] = 1.0f;
                    placed.getRefData().setDynamic(dbs);

                    mLastResult = "spawned dynamic " + mSelected.toDebugString();
                }
                else
                {
                    mLastResult = "spawned static (no physics backend)";
                }
            }
            else
            {
                mLastResult = "spawned " + mSelected.toDebugString();
            }
        }
        catch (const std::exception& e)
        {
            mLastResult = std::string("failed: ") + e.what();
        }
    }
}
