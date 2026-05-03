#ifndef MWGUI_MATERIALEDITOR_H
#define MWGUI_MATERIALEDITOR_H

namespace MWGui
{
    // Phase 8b of docs/imgui-overlay-plan.md — ImGui pane on top of
    // the Material::Registry (Phase 8a). Lists every loaded material,
    // exposes its uniforms with type-dispatched widgets, and pushes
    // edits live via Shader::ShaderManager::triggerShaderReload.
    //
    // MVP scope: in-memory tweaking only. Edits do NOT write back to
    // the YAML on disk (a "Save to YAML" button comes in Phase 8c).
    class MaterialEditor
    {
    public:
        void draw();

    private:
        int mSelected = -1;
    };
}

#endif
