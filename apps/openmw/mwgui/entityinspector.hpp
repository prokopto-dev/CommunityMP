#ifndef MWGUI_ENTITYINSPECTOR_H
#define MWGUI_ENTITYINSPECTOR_H

#include <cstdint>
#include <string>
#include <vector>

#include "../mwworld/ptr.hpp"

namespace MWGui
{
    // ImGui-driven debug panel that lists active-cell references
    // with type / distance / refId filters and shows a detail pane
    // for the current selection. Phase 3 of docs/imgui-overlay-plan.md.
    //
    // Lives inside ImGuiOverlay's draw callback; no GL state of its
    // own. Selection is held as a Ptr which is re-validated against
    // the rebuilt active-cell snapshot each frame so unloaded cells
    // don't leave a dangling reference.
    class EntityInspector
    {
    public:
        EntityInspector();
        void draw();

        // Pick mode: while true, the next world-space left-click
        // (forwarded by ImGuiOverlay::processEvent) becomes a
        // camera-to-cursor raycast that swaps the selection. Cancel
        // with another P press or by clicking the toggle button.
        bool isPickMode() const { return mPickMode; }
        void cancelPickMode() { mPickMode = false; }

        // Phase 8b-octies — same idea but the click resolves to a
        // terrain chunk (worldspace + cellX + cellY + layer textures
        // at hit). Mutually exclusive with regular pick mode for
        // event handling; the overlay's processEvent treats either
        // as pick state.
        bool isPickTerrainMode() const { return mPickTerrainMode; }
        void cancelPickTerrainMode() { mPickTerrainMode = false; }

        // Called by ImGuiOverlay when a click is captured in pick
        // mode. Coordinates are normalized (0..1, top-left origin).
        void onWorldPick(float normalizedX, float normalizedY);
        void onTerrainPick(float normalizedX, float normalizedY);

        // Phase 8b-nonies — main menu bar toggles share this bool
        // with the window's close button (passed as `&mVisible` to
        // ImGui::Begin). Returning a reference lets the menu bar
        // bind a MenuItem checkmark directly.
        bool& visibleFlag() { return mVisible; }

    private:
        // Default true so F1 has the Entity Inspector ready to use
        // — it's the entry point to picking + material editing.
        bool mVisible = true;
        MWWorld::Ptr mSelected;
        bool mPickMode = false;

        // Phase 8b-bis — last YAML override path written via the
        // "Save as YAML override" button, shown next to the button
        // for confirmation.
        std::string mLastOverridePath;

        // Phase 8b-quinquies — multi-slot picker state. mSelectedSlot
        // is the index inside the freshly-collected slot vector but
        // is only a hint; mSelectedSlotKey ("<nodePath>|<diffuse>")
        // is the authoritative identity, re-resolved each frame so a
        // cell reload that churns OSG pointers doesn't leave us
        // pointing at the wrong slot.
        int mSelectedSlot = -1;
        std::string mSelectedSlotKey;

        // Scope mask used by the "Create override" picker — bitwise
        // OR of MWGui::MaterialScope flags. Defaults to per-record-id
        // so the legacy single-click flow still produces an override
        // scoped exactly to the picked entity.
        std::uint32_t mPendingScopeFlags = 1u << 2; // Scope_PerRecord

        // Phase 8b-octies — terrain picking state.
        bool mPickTerrainMode = false;
        struct TerrainPick
        {
            bool mValid = false;
            std::string mWorldspace; // lower-case, ready for matchTerrain
            int mCellX = 0;
            int mCellY = 0;
            std::vector<std::string> mLayerDiffuses;
            std::vector<std::string> mLayerNormals;
            bool mAnyParallax = false;
        };
        TerrainPick mTerrainPick;
        bool mTerrainPerWorldspace = false; // scope picker for terrain overrides

        // Filters
        char mNameFilter[128] = {};
        float mMaxDistance = 5000.0f;
        bool mShowStatic = false;
        bool mShowActivator = true;
        bool mShowDoor = true;
        bool mShowNpc = true;
        bool mShowCreature = true;
        bool mShowContainer = true;
        bool mShowLight = false;
        bool mShowMisc = true;
        bool mShowOther = true;
    };
}

#endif
