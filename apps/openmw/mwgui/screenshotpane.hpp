#ifndef MWGUI_SCREENSHOTPANE_H
#define MWGUI_SCREENSHOTPANE_H

#include <string>

namespace MWGui
{
    class ImGuiOverlay;

    // Phase 8b-sexies — small ImGui pane that triggers the engine's
    // existing screenshot pipeline (osgViewer::ScreenCaptureHandler →
    // SceneUtil::WriteScreenshotToFileOperation, async write to
    // <userdata>/screenshots/) without forcing the user to bind F12.
    //
    // Two modes:
    //   - Direct: fires the action immediately; the overlay shows up
    //     in the captured frame.
    //   - Clean: asks ImGuiOverlay to skip a few drawImplementation
    //     passes so the overlay (and its panes) are absent from the
    //     captured framebuffer.
    class ScreenshotPane
    {
    public:
        // Receives a pointer to the owning overlay so the "clean"
        // mode can request skip-frames. May be nullptr in tests.
        explicit ScreenshotPane(ImGuiOverlay* overlay);

        void draw();
        bool& visibleFlag() { return mVisible; }

    private:
        ImGuiOverlay* mOverlay;
        bool mHideOverlayDuringCapture = true;
        std::string mLastStatus;
        bool mVisible = false;
    };
}

#endif
