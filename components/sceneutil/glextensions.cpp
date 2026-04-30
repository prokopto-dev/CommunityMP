#include "glextensions.hpp"

#include <osg/GraphicsContext>

namespace SceneUtil
{
    namespace
    {
        std::set<osg::observer_ptr<osg::GLExtensions>> sGLExtensions;

        class GLExtensionsObserver : public osg::Observer
        {
        public:
            static GLExtensionsObserver sInstance;

            ~GLExtensionsObserver() override
            {
                for (auto& ptr : sGLExtensions)
                {
                    osg::ref_ptr<osg::GLExtensions> ref;
                    if (ptr.lock(ref))
                        ref->removeObserver(this);
                }
            }

            void objectDeleted(void* referenced) override
            {
                sGLExtensions.erase(static_cast<osg::GLExtensions*>(referenced));
            }
        };

        // construct after sGLExtensions so this gets destroyed first.
        GLExtensionsObserver GLExtensionsObserver::sInstance{};
    }

    bool glExtensionsReady()
    {
        return !sGLExtensions.empty();
    }

    osg::GLExtensions& getGLExtensions()
    {
        if (sGLExtensions.empty())
            throw std::runtime_error(
                "GetGLExtensionsOperation was not used when the current context was created or there is no current "
                "context");
        return **sGLExtensions.begin();
    }

    bool isTessellationSupported()
    {
        if (!glExtensionsReady())
            return false;
        const osg::GLExtensions& ext = getGLExtensions();
        // Hardware tessellation requires GL 4.0 or GL_ARB_tessellation_shader.
        // OSG exposes glPatchParameteri through ext.glPatchParameteri once the
        // extension or core 4.0 is available.
        return ext.glVersion >= 4.0f && ext.glPatchParameteri != nullptr;
    }

    GetGLExtensionsOperation::GetGLExtensionsOperation()
        : GraphicsOperation("GetGLExtensionsOperation", false)
    {
    }

    void GetGLExtensionsOperation::operator()(osg::GraphicsContext* graphicsContext)
    {
        auto [itr, _] = sGLExtensions.emplace(graphicsContext->getState()->get<osg::GLExtensions>());
        (*itr)->addObserver(&GLExtensionsObserver::sInstance);
    }
}
