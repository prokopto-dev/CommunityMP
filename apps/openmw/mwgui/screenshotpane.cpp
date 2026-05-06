#include "screenshotpane.hpp"

#include <imgui.h>

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwinput/actions.hpp"

#include "imguioverlay.hpp"

namespace MWGui
{
    ScreenshotPane::ScreenshotPane(ImGuiOverlay* overlay)
        : mOverlay(overlay)
    {
    }

    void ScreenshotPane::draw()
    {
        if (!mVisible)
            return;
        ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Screenshot", &mVisible))
        {
            ImGui::End();
            return;
        }

        ImGui::TextWrapped(
            "Captures the framebuffer through the same path as F12, "
            "writing to the configured screenshots directory.");

        // When ticked, the button hides the overlay for a couple of
        // frames so the captured image doesn't show ImGui chrome.
        // ScreenCaptureHandler::captureNextFrame schedules the grab
        // for the next rendered frame; two skipped draws gives a
        // safety margin for swap timing.
        ImGui::Checkbox("Hide overlay during capture", &mHideOverlayDuringCapture);

        if (ImGui::Button("Take screenshot"))
        {
            if (mHideOverlayDuringCapture && mOverlay != nullptr)
                mOverlay->requestSkipFrames(2);

            // getInputManager() returns Misc::NotNullPtr — never null
            // by contract, so dereference directly.
            MWBase::Environment::get().getInputManager()->executeAction(MWInput::A_Screenshot);
            mLastStatus = mHideOverlayDuringCapture
                ? "captured (overlay hidden) — saved to userdata/screenshots/"
                : "captured (overlay included) — saved to userdata/screenshots/";
        }

        if (!mLastStatus.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("→ %s", mLastStatus.c_str());
        }

        ImGui::End();
    }
}
