#ifndef OPENMW_COMPONENTS_SCENEUTIL_GLEXTENSIONS_H
#define OPENMW_COMPONENTS_SCENEUTIL_GLEXTENSIONS_H

#include <osg/GLExtensions>
#include <osg/GraphicsThread>

namespace SceneUtil
{
    bool glExtensionsReady();
    osg::GLExtensions& getGLExtensions();

    /// Returns true if the current GL context supports hardware tessellation
    /// (GL 4.0+ or GL_ARB_tessellation_shader). Must be called after the
    /// graphics context has been initialized.
    bool isTessellationSupported();

    class GetGLExtensionsOperation : public osg::GraphicsOperation
    {
    public:
        GetGLExtensionsOperation();

        void operator()(osg::GraphicsContext* graphicsContext) override;
    };
}

#endif
