#ifndef MWGUI_IMGUIOVERLAY_H
#define MWGUI_IMGUIOVERLAY_H

#include <osg/ref_ptr>

namespace osg
{
    class Camera;
}

struct SDL_Window;
union SDL_Event;

namespace MWGui
{
    // In-engine ImGui overlay. Phase 1 of docs/imgui-overlay-plan.md:
    // wires ImGui's SDL2 + OpenGL3 backends behind a hotkey toggle
    // (default F1) so we have a debug surface for entity inspection,
    // physics tracing, and runtime tuning that lives outside the
    // MyGUI tree.
    //
    // Lifetime: constructed once after the SDL window + OSG viewer
    // are ready. Render is hooked as a post-draw callback on the
    // main camera. Input is fed via processEvent before SDLUtil
    // dispatches the event further.
    class ImGuiOverlay
    {
    public:
        ImGuiOverlay(SDL_Window* window, osg::Camera* mainCamera);
        ~ImGuiOverlay();

        ImGuiOverlay(const ImGuiOverlay&) = delete;
        ImGuiOverlay& operator=(const ImGuiOverlay&) = delete;

        // Forward an SDL event to ImGui. Returns true if ImGui wants
        // to consume the event (e.g. typing into a text field) so
        // the caller can short-circuit normal game-input dispatch.
        bool processEvent(const SDL_Event& event);

        // Toggle visibility. Hotkey wiring lives in keyboardmanager.cpp.
        void toggleVisible() { mVisible = !mVisible; }
        bool isVisible() const { return mVisible; }

        // Whether ImGui currently wants to capture keyboard / mouse.
        // Game-input code uses these to gate WASD / mouse-look while
        // the user is interacting with an ImGui widget.
        bool wantsKeyboard() const;
        bool wantsMouse() const;

    private:
        // Forward-declared OSG camera that runs ImGui rendering as a
        // post-draw callback. Owned by the camera's parent node.
        osg::ref_ptr<osg::Camera> mCamera;

        bool mVisible = false;
        bool mInitialized = false;
    };
}

#endif
