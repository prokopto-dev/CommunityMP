#ifndef MWGUI_MATERIALEDITOR_H
#define MWGUI_MATERIALEDITOR_H

namespace Material
{
    struct MaterialDef;
}

namespace MWGui
{
    // Render an inline editor for a single MaterialDef (priority,
    // shader prefix, defines, uniforms, match rules). Returns true
    // if the user changed any value — caller is responsible for
    // triggering a shader reload (and optionally Registry::resort()
    // when priority changed). Shared between the standalone
    // "Materials" window and the EntityInspector's per-Ptr Material
    // pane (Phase 8b-bis / 8b-ter).
    bool drawMaterialDefInline(Material::MaterialDef& def);

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
