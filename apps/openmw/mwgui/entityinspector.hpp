#ifndef MWGUI_ENTITYINSPECTOR_H
#define MWGUI_ENTITYINSPECTOR_H

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

        // Called by ImGuiOverlay when a click is captured in pick
        // mode. Coordinates are normalized (0..1, top-left origin).
        void onWorldPick(float normalizedX, float normalizedY);

    private:
        MWWorld::Ptr mSelected;
        bool mPickMode = false;

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
