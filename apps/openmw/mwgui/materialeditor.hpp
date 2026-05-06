#ifndef MWGUI_MATERIALEDITOR_H
#define MWGUI_MATERIALEDITOR_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace osg
{
    class Node;
    class StateSet;
}

namespace Material
{
    struct MaterialDef;
}

namespace MWGui
{
    // Phase 8b-septies — context the EntityInspector hands to
    // drawMaterialDefInline so the parallax helper block can colour
    // its status indicator and seed Auto-detect from the slot's
    // diffuse texture name. Empty / default-constructed when called
    // from the standalone Materials window (no slot context) — the
    // helper falls back to a plain slider in that case.
    struct ParallaxHint
    {
        bool mHasHeightmap = false;       // mirrors MaterialSlot::mHasHeightInNormalAlpha
        std::string mDiffuseBasename;     // basename, used for Auto-detect pattern match
    };

    // Render an inline editor for a single MaterialDef (priority,
    // shader prefix, defines, uniforms, match rules). Returns true
    // if the user changed any value — caller is responsible for
    // triggering a shader reload (and optionally Registry::resort()
    // when priority changed). Shared between the standalone
    // "Materials" window and the EntityInspector's per-Ptr Material
    // pane (Phase 8b-bis / 8b-ter).
    bool drawMaterialDefInline(Material::MaterialDef& def, const ParallaxHint& hint = {});

    // Phase 8b-quinquies — one row in the multi-slot picker. A "slot"
    // is a unique StateSet found anywhere in an entity's BaseNode
    // subtree. The visitor dedupes by StateSet pointer (StateSets are
    // shared across drawables of a NIF clone) and surfaces the texture
    // set + node path so the editor can build per-child / per-texture
    // overrides without forcing the user to know the OSG hierarchy.
    struct MaterialSlot
    {
        const osg::StateSet* mStateSetKey = nullptr;
        std::string mNodePath;       // joined names "/Bip01/Hair/Tri Hair"
        std::string mNodeName;       // last non-empty name on the path
        std::string mDrawableName;   // for anonymous fallback
        std::string mDiffuse;
        std::string mNormal;
        std::string mSpecular;
        std::string mBump;
        bool mHasHeightInNormalAlpha = false;
        bool mAnonymous = false; // true → "Per child" scope unavailable
    };

    // Walk the entity's subtree and return one slot per distinct
    // StateSet that carries at least one classified texture
    // (diffuse/normal/specular/bump). Order is depth-first stable so
    // re-resolving by index is safe across frames.
    std::vector<MaterialSlot> collectMaterialSlots(osg::Node& root);

    // Scope flags for makeMaterialDefForSlot — combinable; each set
    // bit produces one MatchRule appended to the new MaterialDef so
    // the rules OR together at lookup time.
    enum MaterialScope : std::uint32_t
    {
        Scope_PerChild = 1u << 0,   // rule on slot.mNodeName
        Scope_PerTexture = 1u << 1, // rule on slot.mDiffuse basename
        Scope_PerRecord = 1u << 2,  // rule on entity refId
        Scope_PerMesh = 1u << 3,    // rule on the mesh basename
    };

    // Build a fresh MaterialDef for a given slot + scope mask.
    // Generated name = "<sanitisedRefId>__<sanitisedSlotKey>" so a
    // multi-def YAML file per entity stays unambiguous. Priority is
    // 100 (above the legacy "<refId>_override" path's 100 — duplicates
    // are deduped by name in Registry::add). Seeded with one starter
    // uniform (parallaxScale) so a fresh override has something to
    // tweak immediately.
    Material::MaterialDef makeMaterialDefForSlot(const MaterialSlot& slot, std::uint32_t scopeFlags,
        const std::string& refId, const std::string& meshBasename, float parallaxScaleSeed);

    // Phase 8b-octies — fresh MaterialDef for a terrain pick. The
    // perWorldspace flag flips between a per-cell rule (cells:[{x,y}])
    // and a worldspace-wide rule (cells:[] = wildcard). Generated
    // name = "terrain__<worldspace>__<x>_<y>" or "terrain__<worldspace>"
    // for the wildcard variant.
    Material::MaterialDef makeTerrainOverride(const std::string& worldspace, int cellX, int cellY,
        bool perWorldspace, float parallaxScaleSeed);

    // Serialize a list of MaterialDef pointers to a multi-def YAML
    // file at `path`. Top-level shape is `materials: [...]`. Emits all
    // four match keys (mesh / node / texture / record_id) so a saved
    // override round-trips losslessly.
    bool writeEntityOverrideYaml(const std::filesystem::path& path, const std::string& refId,
        const std::vector<const Material::MaterialDef*>& defs);

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
        bool& visibleFlag() { return mVisible; }

    private:
        int mSelected = -1;
        bool mVisible = false;
    };
}

#endif
