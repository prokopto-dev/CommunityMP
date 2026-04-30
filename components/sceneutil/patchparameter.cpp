#include "patchparameter.hpp"

#include <osg/GLExtensions>
#include <osg/State>

#ifndef GL_PATCH_VERTICES
#define GL_PATCH_VERTICES 0x8E72
#endif

namespace SceneUtil
{
    void PatchParameter::apply(osg::State& state) const
    {
        const osg::GLExtensions* ext = state.get<osg::GLExtensions>();
        if (ext && ext->glPatchParameteri)
            ext->glPatchParameteri(GL_PATCH_VERTICES, mVertices);
    }
}
