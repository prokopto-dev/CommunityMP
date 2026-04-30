#ifndef OPENMW_COMPONENTS_SCENEUTIL_PATCHPARAMETER_H
#define OPENMW_COMPONENTS_SCENEUTIL_PATCHPARAMETER_H

#include <osg/StateAttribute>

namespace SceneUtil
{
    /// Minimal StateAttribute that issues glPatchParameteri(GL_PATCH_VERTICES, n)
    /// when the state is applied. Avoids depending on osg::PatchParameter, which
    /// is only present in newer OSG releases. The attribute does nothing on GL
    /// contexts that lack tessellation support.
    class PatchParameter : public osg::StateAttribute
    {
    public:
        PatchParameter()
            : mVertices(3)
        {
        }
        explicit PatchParameter(int vertices)
            : mVertices(vertices)
        {
        }
        PatchParameter(const PatchParameter& copy, const osg::CopyOp& copyop = osg::CopyOp::SHALLOW_COPY)
            : osg::StateAttribute(copy, copyop)
            , mVertices(copy.mVertices)
        {
        }

        META_StateAttribute(SceneUtil, PatchParameter, osg::StateAttribute::Type(10001));

        int compare(const osg::StateAttribute& sa) const override
        {
            COMPARE_StateAttribute_Types(PatchParameter, sa)
            COMPARE_StateAttribute_Parameter(mVertices)
            return 0;
        }

        void apply(osg::State& state) const override;

        int getVertices() const { return mVertices; }
        void setVertices(int v) { mVertices = v; }

    private:
        int mVertices;
    };
}

#endif
