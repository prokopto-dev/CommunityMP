#ifndef MWGUI_IMGUIOVERLAY_H
#define MWGUI_IMGUIOVERLAY_H

#include <memory>

#include <osg/ref_ptr>

namespace osg
{
    class Camera;
    class Group;
}

namespace SDLUtil
{
    class InputWrapper;
}

struct SDL_Window;
union SDL_Event;

namespace MWGui
{
    class EntityInspector;
    class MaterialEditor;
    class ObjectSpawner;
    class ShaderSettings;

    // In-engine ImGui overlay. Phase 1 of docs/imgui-overlay-plan.md:
    // wires ImGui's SDL2 + OpenGL2 backends behind a hotkey toggle
    // (default F1) so we have a debug surface for entity inspection,
    // physics tracing, and runtime tuning that lives outside the
    // MyGUI tree.
    //
    // Lifetime: constructed once after the SDL window + OSG viewer
    // are ready. Rendering is hooked as a post-render OSG camera
    // attached to guiRoot (same level as MyGUI). Input is fed via
    // processEvent before SDLUtil dispatches the event further.
    class ImGuiOverlay
    {
    public:
        ImGuiOverlay(SDL_Window* window, osg::Group* guiRoot);
        ~ImGuiOverlay();

        ImGuiOverlay(const ImGuiOverlay&) = delete;
        ImGuiOverlay& operator=(const ImGuiOverlay&) = delete;

        // Bind to the input wrapper so toggleVisible() can release
        // the cursor (force visible + non-relative) while the
        // overlay is up. Called by Engine after InputManager exists.
        void attachInputWrapper(SDLUtil::InputWrapper* wrapper) { mInputWrapper = wrapper; }

        // Forward an SDL event to ImGui. Returns true if ImGui wants
        // to consume the event (e.g. typing into a text field) so
        // the caller can short-circuit normal game-input dispatch.
        bool processEvent(const SDL_Event& event);

        // Toggle visibility. Releases / restores cursor as a side
        // effect when an InputWrapper is attached.
        void toggleVisible();
        bool isVisible() const { return mVisible; }

        // Whether ImGui currently wants to capture keyboard / mouse.
        // Game-input code uses these to gate WASD / mouse-look while
        // the user is interacting with an ImGui widget.
        bool wantsKeyboard() const;
        bool wantsMouse() const;

        // Lazy-init flag for the OpenGL2 backend. Mutated from the
        // OSG draw thread on first render — the only place a GL
        // context is reliably current.
        bool isGLInitialized() const { return mGLInitialized; }
        void markGLInitialized() { mGLInitialized = true; }

        // Accessor used by MWInput::MouseManager to keep the cursor
        // free while the overlay is up — see updateCursorMode.
        // Single-process singleton: only one overlay at a time.
        static ImGuiOverlay* instance() { return sInstance; }

        // True while the user is holding right-click to drive the
        // game camera through the overlay. MouseManager treats this
        // as "let relative mode through" so look-around still works
        // without closing the inspector.
        bool isCameraDragging() const { return mCameraDrag; }

        EntityInspector* entityInspector() { return mEntityInspector.get(); }
        ObjectSpawner* objectSpawner() { return mObjectSpawner.get(); }
        MaterialEditor* materialEditor() { return mMaterialEditor.get(); }
        ShaderSettings* shaderSettings() { return mShaderSettings.get(); }

    private:
        osg::ref_ptr<osg::Camera> mCamera;
        SDLUtil::InputWrapper* mInputWrapper = nullptr;
        std::unique_ptr<EntityInspector> mEntityInspector;
        std::unique_ptr<ObjectSpawner> mObjectSpawner;
        std::unique_ptr<MaterialEditor> mMaterialEditor;
        std::unique_ptr<ShaderSettings> mShaderSettings;

        bool mVisible = false;
        bool mInitialized = false;
        bool mGLInitialized = false;
        bool mCameraDrag = false;

        static ImGuiOverlay* sInstance;
    };
}

#endif
