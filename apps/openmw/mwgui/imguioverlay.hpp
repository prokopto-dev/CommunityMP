#ifndef MWGUI_IMGUIOVERLAY_H
#define MWGUI_IMGUIOVERLAY_H

#include <atomic>
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
    class ScreenshotPane;
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

        // The OSG draw thread snapshots ImGui::GetIO().WantTextInput
        // into mPendingTextInput each frame. Engine::frame calls
        // pumpMainThread() on the main thread, which applies the
        // pending state via SDL_Start/StopTextInput. Required on
        // macOS: those SDL calls hit NSTextInputContext, which
        // throws an Objective-C exception when invoked off the
        // Cocoa main thread (terminates the process).
        void setPendingTextInput(bool want) { mPendingTextInput.store(want ? 1 : 0, std::memory_order_release); }
        void pumpMainThread();

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
        ScreenshotPane* screenshotPane() { return mScreenshotPane.get(); }

        // Phase 8b-sexies — ask the drawable to skip the next `n`
        // ImGui draw passes. Used by ScreenshotPane's "Hide overlay
        // during capture" path so the captured framebuffer doesn't
        // contain ImGui chrome. Stored atomically because it's read
        // on the OSG draw thread and written from the main thread
        // (ImGui callbacks run on the draw thread, but the screenshot
        // pipeline schedules the capture for a *future* frame).
        void requestSkipFrames(int frames) { mSkipFrames.store(frames, std::memory_order_release); }

        // True when at least one skip frame is pending — drawable
        // gates its early return on this AND mVisible.
        bool consumeSkipFrame()
        {
            int v = mSkipFrames.load(std::memory_order_acquire);
            while (v > 0)
            {
                if (mSkipFrames.compare_exchange_weak(v, v - 1, std::memory_order_acq_rel))
                    return true;
            }
            return false;
        }

    private:
        osg::ref_ptr<osg::Camera> mCamera;
        SDLUtil::InputWrapper* mInputWrapper = nullptr;
        std::unique_ptr<EntityInspector> mEntityInspector;
        std::unique_ptr<ObjectSpawner> mObjectSpawner;
        std::unique_ptr<MaterialEditor> mMaterialEditor;
        std::unique_ptr<ShaderSettings> mShaderSettings;
        std::unique_ptr<ScreenshotPane> mScreenshotPane;

        bool mVisible = false;
        bool mInitialized = false;
        bool mGLInitialized = false;
        bool mCameraDrag = false;
        std::atomic<int> mPendingTextInput{ -1 };
        std::atomic<int> mSkipFrames{ 0 };

        static ImGuiOverlay* sInstance;
    };
}

#endif
