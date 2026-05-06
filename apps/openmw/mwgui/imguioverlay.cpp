#include "imguioverlay.hpp"

#include <SDL_events.h>
#include <SDL_video.h>

#include <osg/Camera>
#include <osg/Drawable>
#include <osg/GraphicsContext>
#include <osg/Group>
#include <osg/State>

#include <imgui.h>
#include <imgui_impl_opengl2.h>
#include <imgui_impl_sdl2.h>

#include <components/debug/debuglog.hpp>
#include <components/material/materialregistry.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sdlutil/sdlinputwrapper.hpp>
#include <components/shader/shadermanager.hpp>
#include <components/vfs/manager.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwrender/objects.hpp"
#include "../mwrender/renderingmanager.hpp"

#include "entityinspector.hpp"
#include "materialeditor.hpp"
#include "objectspawner.hpp"
#include "screenshotpane.hpp"
#include "shadersettings.hpp"

namespace MWGui
{
    namespace
    {
        // Phase 8b-nonies — main menu bar with View / Tools / Help
        // sections. Drawn first each frame so the bar sits above the
        // pane windows; toggling visibility writes through references
        // so the X-close button on each pane mirrors the menu state.
        void drawMainMenuBar(ImGuiOverlay& overlay)
        {
            if (!ImGui::BeginMainMenuBar())
                return;

            if (ImGui::BeginMenu("View"))
            {
                if (auto* p = overlay.entityInspector())
                    ImGui::MenuItem("Entity Inspector", nullptr, &p->visibleFlag());
                if (auto* p = overlay.materialEditor())
                    ImGui::MenuItem("Material Editor", nullptr, &p->visibleFlag());
                if (auto* p = overlay.objectSpawner())
                    ImGui::MenuItem("Object Spawner", nullptr, &p->visibleFlag());
                if (auto* p = overlay.shaderSettings())
                    ImGui::MenuItem("Shader Settings", nullptr, &p->visibleFlag());
                if (auto* p = overlay.screenshotPane())
                    ImGui::MenuItem("Screenshot", nullptr, &p->visibleFlag());
                ImGui::Separator();
                if (ImGui::MenuItem("Hide all"))
                {
                    if (auto* p = overlay.entityInspector())
                        p->visibleFlag() = false;
                    if (auto* p = overlay.materialEditor())
                        p->visibleFlag() = false;
                    if (auto* p = overlay.objectSpawner())
                        p->visibleFlag() = false;
                    if (auto* p = overlay.shaderSettings())
                        p->visibleFlag() = false;
                    if (auto* p = overlay.screenshotPane())
                        p->visibleFlag() = false;
                }
                if (ImGui::MenuItem("Show all"))
                {
                    if (auto* p = overlay.entityInspector())
                        p->visibleFlag() = true;
                    if (auto* p = overlay.materialEditor())
                        p->visibleFlag() = true;
                    if (auto* p = overlay.objectSpawner())
                        p->visibleFlag() = true;
                    if (auto* p = overlay.shaderSettings())
                        p->visibleFlag() = true;
                    if (auto* p = overlay.screenshotPane())
                        p->visibleFlag() = true;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Tools"))
            {
                auto* sceneMgr
                    = MWBase::Environment::get().getResourceSystem()->getSceneManager();
                if (ImGui::MenuItem("Reload shaders"))
                {
                    if (sceneMgr)
                        sceneMgr->getShaderManager().triggerShaderReload();
                }
                if (ImGui::MenuItem("Recreate shaders (full pass)"))
                {
                    if (sceneMgr)
                    {
                        if (auto* r = MWBase::Environment::get().getWorld()->getRenderingManager())
                            if (auto* root = r->getObjects().getRootNode())
                                sceneMgr->recreateShaders(root);
                    }
                }
                if (ImGui::MenuItem("Reload materials from disk"))
                {
                    if (sceneMgr && sceneMgr->getMaterialRegistry())
                    {
                        sceneMgr->getMaterialRegistry()->reload(
                            MWBase::Environment::get().getResourceSystem()->getVFS());
                        sceneMgr->getShaderManager().triggerShaderReload();
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help"))
            {
                ImGui::TextDisabled("F1            toggle overlay");
                ImGui::TextDisabled("Esc           cancel pick mode");
                ImGui::TextDisabled("Right-click   drag world camera through overlay");
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }
    }

    ImGuiOverlay* ImGuiOverlay::sInstance = nullptr;

    namespace
    {
        // Drawable that runs the ImGui render in the OSG draw thread.
        // Attached to a HUD camera that runs after the main scene
        // camera so ImGui sits on top of everything game-rendered.
        //
        // Backend choice (OpenGL2 fixed-function) matters: it wraps
        // its draws in glPushAttrib(GL_ENABLE_BIT |
        // GL_COLOR_BUFFER_BIT | GL_TRANSFORM_BIT) and restores
        // viewport / scissor / matrices around the call, so OSG's
        // GL state is left intact across frames.
        class ImGuiDrawable : public osg::Drawable
        {
        public:
            ImGuiDrawable(ImGuiOverlay& owner)
                : mOwner(&owner)
            {
                setSupportsDisplayList(false);
                setUseVertexBufferObjects(false);
                setDataVariance(osg::Object::DYNAMIC);
            }

            void drawImplementation(osg::RenderInfo& renderInfo) const override
            {
                if (!mOwner->isVisible())
                    return;
                // ScreenshotPane requests skip frames when capturing
                // a clean shot — eat one each pass and bail before
                // any ImGui state mutation so the framebuffer that
                // ScreenCaptureHandler grabs has no overlay chrome.
                if (mOwner->consumeSkipFrame())
                    return;
                if (!mOwner->isGLInitialized())
                {
                    if (!ImGui_ImplOpenGL2_Init())
                        return;
                    mOwner->markGLInitialized();
                }

                // Isolate from OSG GL state. The OpenGL2 backend
                // assumes the active texture unit is 0 and that no
                // VBO is bound — neither holds after a full OSG
                // scene render. Without this, font glyphs render as
                // untextured boxes (the fixed-function sampler reads
                // from whatever unit OSG happened to leave active).
                osg::State* state = renderInfo.getState();
                state->disableAllVertexArrays();
                state->setActiveTextureUnit(0);
                state->setClientActiveTextureUnit(0);
                state->unbindVertexBufferObject();
                state->unbindElementBufferObject();

                ImGui_ImplOpenGL2_NewFrame();
                ImGui_ImplSDL2_NewFrame();
                ImGui::NewFrame();
                drawMainMenuBar(*mOwner);
                if (auto* inspector = mOwner->entityInspector())
                    inspector->draw();
                if (auto* spawner = mOwner->objectSpawner())
                    spawner->draw();
                if (auto* materials = mOwner->materialEditor())
                    materials->draw(); // includes <materialeditor.hpp> below
                if (auto* shaderSettings = mOwner->shaderSettings())
                    shaderSettings->draw();
                if (auto* screenshot = mOwner->screenshotPane())
                    screenshot->draw();
                ImGui::Render();
                ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

                // Phase 8b-quater — manage SDL text input mode
                // ourselves: the imgui_impl_sdl2 backend stopped
                // touching SDL_StartTextInput in 2023, and OpenMW's
                // own UI code (MyGUI) only flips it for vanilla
                // widgets. Without this, typing into an ImGui
                // InputText produces no TEXTINPUT events on macOS.
                //
                // On macOS, SDL_Start/StopTextInput call into
                // NSTextInputContext, which raises an Objective-C
                // exception when invoked from any thread but the
                // Cocoa main thread — and we're on the OSG draw
                // thread here. Snapshot the desired state into an
                // atomic; ImGuiOverlay::pumpMainThread() (called
                // from Engine::frame) applies it from the right
                // thread.
                mOwner->setPendingTextInput(ImGui::GetIO().WantTextInput);

                // Tell OSG every cached vertex array / texture state
                // is now stale so it re-applies on the next frame.
                state->dirtyAllVertexArrays();
                state->dirtyAllAttributes();
                state->disableAllVertexArrays();
            }

        private:
            ImGuiOverlay* mOwner;
        };
    }

    ImGuiOverlay::ImGuiOverlay(SDL_Window* window, osg::Group* guiRoot)
    {
        if (window == nullptr || guiRoot == nullptr)
        {
            Log(Debug::Warning) << "ImGuiOverlay: missing window or guiRoot";
            return;
        }
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; // no on-disk config; sessions are clean
        // ImGui_ImplSDL2_NewFrame runs on the OSG draw thread; macOS
        // ignores SDL_ShowCursor / SDL_SetCursor calls from a thread
        // that isn't the Cocoa main thread. Disable ImGui's OS-cursor
        // mutation entirely.
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        // OpenMW keeps the OS cursor hidden during gameplay (MyGUI
        // draws its own sprite-based cursor in menus). Tell ImGui to
        // render its own software cursor so the overlay always has a
        // visible pointer regardless of the OS cursor state.
        io.MouseDrawCursor = true;

        // Scale fonts and widget metrics to the display's pixel ratio
        // so the overlay reads correctly on Retina / HiDPI panels.
        // ImGui_ImplSDL2_NewFrame already sets DisplayFramebufferScale
        // for crisp rasterization, but UI metrics stay at logical
        // pixels by default — too small on a 4-5K screen.
        int windowW = 0, windowH = 0, drawableW = 0, drawableH = 0;
        SDL_GetWindowSize(window, &windowW, &windowH);
        SDL_GL_GetDrawableSize(window, &drawableW, &drawableH);
        const float dpiScale = (windowW > 0) ? (static_cast<float>(drawableW) / windowW) : 1.0f;
        // ImGui 1.92 replaced io.FontGlobalScale with style.FontScaleMain.
        ImGui::GetStyle().FontScaleMain = dpiScale;
        ImGui::GetStyle().ScaleAllSizes(dpiScale);

        ImGui_ImplSDL2_InitForOpenGL(window, nullptr);
        // OpenGL2 backend init is deferred to first draw — it's the
        // cleanest moment to be sure a GL context is current. The
        // backend itself loads no symbols (fixed-function only), so
        // the deferral is just for symmetry / safety.

        // HUD camera attached to guiRoot, mirroring how MyGUI's
        // RenderManager hooks itself in. Attaching to mViewer's
        // master camera doesn't work: the post-processor renders
        // the master into an FBO, so a child of the master never
        // reaches the screen framebuffer. guiRoot stays in the
        // scene graph at the root level and is visible regardless
        // of the post-processing pipeline.
        mCamera = new osg::Camera;
        mCamera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
        mCamera->setProjectionResizePolicy(osg::Camera::FIXED);
        mCamera->setProjectionMatrix(osg::Matrix::identity());
        mCamera->setViewMatrix(osg::Matrix::identity());
        mCamera->setRenderOrder(osg::Camera::POST_RENDER);
        mCamera->setClearMask(GL_NONE);
        mCamera->setAllowEventFocus(false);
        osg::ref_ptr<ImGuiDrawable> drawable = new ImGuiDrawable(*this);
        drawable->setCullingActive(false);
        mCamera->addChild(drawable);
        guiRoot->addChild(mCamera);

        mEntityInspector = std::make_unique<EntityInspector>();
        mObjectSpawner = std::make_unique<ObjectSpawner>();
        mMaterialEditor = std::make_unique<MaterialEditor>();
        mShaderSettings = std::make_unique<ShaderSettings>();
        mScreenshotPane = std::make_unique<ScreenshotPane>(this);

        sInstance = this;
        mInitialized = true;
        Log(Debug::Info) << "ImGuiOverlay: initialised (F1 to toggle, dpiScale=" << dpiScale << ")";
    }

    ImGuiOverlay::~ImGuiOverlay()
    {
        if (sInstance == this)
            sInstance = nullptr;
        if (!mInitialized)
            return;
        // Skip ImGui_ImplOpenGL2_Shutdown / ImGui_ImplSDL2_Shutdown:
        // they touch GL state, but this destructor runs on the main
        // thread where the context isn't current. The process is
        // exiting, so the OS reclaims the GL objects regardless.
        ImGui::DestroyContext();
    }

    void ImGuiOverlay::pumpMainThread()
    {
        if (!mInitialized)
            return;
        const int pending = mPendingTextInput.exchange(-1, std::memory_order_acq_rel);
        if (pending < 0)
            return;
        const bool want = pending != 0;
        const bool active = SDL_IsTextInputActive();
        if (want && !active)
            SDL_StartTextInput();
        else if (!want && active)
            SDL_StopTextInput();
    }

    void ImGuiOverlay::toggleVisible()
    {
        mVisible = !mVisible;
        if (mInputWrapper == nullptr)
            return;
        // While the overlay is up, force the cursor visible and
        // non-relative so the user can actually click on widgets.
        // MouseManager::updateCursorMode also bypasses relative
        // mode while we're shown, so this stays sticky frame to
        // frame. When we hide, the next updateCursorMode tick puts
        // things back the way the current game state wants them.
        mInputWrapper->setMouseRelative(!mVisible);
        mInputWrapper->setMouseVisible(mVisible);
    }

    bool ImGuiOverlay::processEvent(const SDL_Event& event)
    {
        if (!mInitialized || !mVisible)
            return false;

        // Hold right-click in the scene (outside any ImGui widget)
        // to drive the game camera through the overlay. The mouse
        // switches to relative mode for the duration of the hold so
        // mouselook still feels native; ImGui never sees the motion
        // events, so the cursor stays where the user left it.
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT
            && !mCameraDrag && !ImGui::GetIO().WantCaptureMouse)
        {
            mCameraDrag = true;
            if (mInputWrapper)
            {
                mInputWrapper->setMouseRelative(true);
                mInputWrapper->setMouseVisible(false);
            }
            return false; // let the game also see the right-click
        }
        if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_RIGHT && mCameraDrag)
        {
            mCameraDrag = false;
            if (mInputWrapper)
            {
                mInputWrapper->setMouseRelative(false);
                mInputWrapper->setMouseVisible(true);
            }
            return false;
        }
        if (mCameraDrag)
        {
            // Suppress mouse motion / button events from ImGui while
            // dragging — let the game's input pipeline consume them.
            switch (event.type)
            {
                case SDL_MOUSEMOTION:
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP:
                case SDL_MOUSEWHEEL:
                    return false;
                default:
                    break;
            }
        }

        // Pick mode: a left-click outside ImGui widgets becomes a
        // camera-to-cursor raycast that updates the inspector
        // selection (entity or terrain), then exits pick mode. Esc
        // cancels either mode.
        if (mEntityInspector
            && (mEntityInspector->isPickMode() || mEntityInspector->isPickTerrainMode()))
        {
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
            {
                mEntityInspector->cancelPickMode();
                mEntityInspector->cancelPickTerrainMode();
                return true;
            }
            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT
                && !ImGui::GetIO().WantCaptureMouse)
            {
                int winW = 0, winH = 0;
                SDL_GetWindowSize(SDL_GetWindowFromID(event.button.windowID), &winW, &winH);
                if (winW > 0 && winH > 0)
                {
                    const float nX = static_cast<float>(event.button.x) / winW;
                    const float nY = static_cast<float>(event.button.y) / winH;
                    if (mEntityInspector->isPickTerrainMode())
                        mEntityInspector->onTerrainPick(nX, nY);
                    else
                        mEntityInspector->onWorldPick(nX, nY);
                }
                return true;
            }
        }

        ImGui_ImplSDL2_ProcessEvent(&event);
        const ImGuiIO& io = ImGui::GetIO();
        switch (event.type)
        {
            case SDL_KEYDOWN:
            case SDL_KEYUP:
            case SDL_TEXTINPUT:
                return io.WantCaptureKeyboard;
            case SDL_MOUSEMOTION:
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
            case SDL_MOUSEWHEEL:
                return io.WantCaptureMouse;
            default:
                return false;
        }
    }

    bool ImGuiOverlay::wantsKeyboard() const
    {
        if (!mInitialized || !mVisible)
            return false;
        return ImGui::GetIO().WantCaptureKeyboard;
    }

    bool ImGuiOverlay::wantsMouse() const
    {
        if (!mInitialized || !mVisible)
            return false;
        return ImGui::GetIO().WantCaptureMouse;
    }
}
