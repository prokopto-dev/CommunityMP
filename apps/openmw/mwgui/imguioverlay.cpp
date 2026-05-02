#include "imguioverlay.hpp"

#include <SDL_events.h>

#include <osg/Camera>
#include <osg/Drawable>
#include <osg/GraphicsContext>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <components/debug/debuglog.hpp>

namespace MWGui
{
    namespace
    {
        // Drawable that runs the ImGui render in the OSG draw thread.
        // Attached to a HUD camera that runs after the main scene
        // camera so ImGui sits on top of everything game-rendered.
        class ImGuiDrawable : public osg::Drawable
        {
        public:
            ImGuiDrawable(const ImGuiOverlay& owner)
                : mOwner(&owner)
            {
                setSupportsDisplayList(false);
                setUseVertexBufferObjects(false);
                setDataVariance(osg::Object::DYNAMIC);
            }

            void drawImplementation(osg::RenderInfo&) const override
            {
                if (!mOwner->isVisible())
                    return;
                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplSDL2_NewFrame();
                ImGui::NewFrame();
                // Phase 1 sentinel: just show the demo window. Phase 3
                // replaces this with the entity-inspector tree.
                ImGui::ShowDemoWindow();
                ImGui::Render();
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            }

        private:
            const ImGuiOverlay* mOwner;
        };
    }

    ImGuiOverlay::ImGuiOverlay(SDL_Window* window, osg::Camera* mainCamera)
    {
        if (window == nullptr || mainCamera == nullptr)
        {
            Log(Debug::Warning) << "ImGuiOverlay: missing window or camera";
            return;
        }
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; // no on-disk config; sessions are clean

        ImGui_ImplSDL2_InitForOpenGL(window, nullptr);
        // GLSL 130 = OpenGL 3.0; OpenMW targets at least that.
        ImGui_ImplOpenGL3_Init("#version 130");

        // HUD camera that runs after the main scene camera, with
        // ImGuiDrawable as its only geode. Attached to the main
        // camera so it inherits the viewport.
        mCamera = new osg::Camera;
        mCamera->setRenderOrder(osg::Camera::POST_RENDER);
        mCamera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
        mCamera->setClearMask(GL_NONE);
        mCamera->setAllowEventFocus(false);
        mCamera->addChild(new ImGuiDrawable(*this));
        mainCamera->addChild(mCamera);

        mInitialized = true;
        Log(Debug::Info) << "ImGuiOverlay: initialised (F1 to toggle)";
    }

    ImGuiOverlay::~ImGuiOverlay()
    {
        if (!mInitialized)
            return;
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }

    bool ImGuiOverlay::processEvent(const SDL_Event& event)
    {
        if (!mInitialized || !mVisible)
            return false;
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
