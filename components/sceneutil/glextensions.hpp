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

    /// Returns true if the current GL context supports compute shaders
    /// (GL 4.3+ or GL_ARB_compute_shader). Required for SSBO + atomic-driven
    /// raycast features (e.g. world-space point-light shadows). Returns false
    /// on macOS native (capped at GL 4.1).
    bool isComputeSupported();

    class GetGLExtensionsOperation : public osg::GraphicsOperation
    {
    public:
        GetGLExtensionsOperation();

        void operator()(osg::GraphicsContext* graphicsContext) override;
    };
}

#endif
