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
#include <components/sdlutil/sdlinputwrapper.hpp>

#include "entityinspector.hpp"
#include "objectspawner.hpp"

namespace MWGui
{
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
                if (auto* inspector = mOwner->entityInspector())
                    inspector->draw();
                if (auto* spawner = mOwner->objectSpawner())
                    spawner->draw();
                ImGui::Render();
                ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

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
        // selection, then exits pick mode. Esc cancels.
        if (mEntityInspector && mEntityInspector->isPickMode())
        {
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
            {
                mEntityInspector->cancelPickMode();
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
