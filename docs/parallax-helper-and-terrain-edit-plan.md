# Parallax Helper + Terrain Texture Editor

## Contexte

Deux extensions à l'overlay ImGui qui s'appuient sur le pipeline d'override déjà en place (`Material::Registry`, `ShaderVisitor` Phase 8a, `terrainOverride` Phase 8d) :

1. **Helper parallax** — l'utilisateur tape `parallaxScale` au pifomètre. On veut des presets nommés ("stone", "brick", "fabric"…), une ligne de boutons d'incrément rapide, et un indicateur live qui dit si le slot a vraiment un heightmap (sinon `parallaxScale` est un no-op).
2. **Édition terrain dans l'EntityInspector** — la Material section ne s'affiche que pour des Ptrs entité. On veut un mode terrain : viser un chunk au sol via "Pick from world", lire son `(worldspace, cellX, cellY)`, surfacer ses layers de texture, et créer un `TerrainRule` override depuis le picker.

Les deux features réutilisent l'API existante : pas de changements de schéma `MaterialDef`, ni de `ShaderVisitor`, ni de `terrain::material.cpp`.

## Approche

### 1. Helper parallax (per-slot, dans `drawMaterialDefInline`)

**Fichier :** `apps/openmw/mwgui/materialeditor.cpp:228-296` (section "Uniforms")

Un sous-bloc dédié au-dessus de la liste générique des uniforms, déclenché quand le slot a un heightmap utile. La détection est déjà faite côté inspector (`MaterialSlot::mHasHeightInNormalAlpha`) — il faut la propager au bloc parallax via un argument supplémentaire à `drawMaterialDefInline`.

**Signature étendue :**
```cpp
struct ParallaxHint {
    bool mHasHeightmap = false;       // true → parallaxScale fait quelque chose
    std::string mDiffuseBasename;      // pour matcher les presets par pattern
};
bool drawMaterialDefInline(Material::MaterialDef& def, const ParallaxHint& hint = {});
```

Le `default {}` garde la backward-compat avec la fenêtre Materials standalone (qui n'a pas de slot context).

**UI parallax dans `drawMaterialDefInline` (nouveau bloc avant les uniforms génériques) :**

- **Slider principal** : `DragFloat("parallaxScale", &p, 0.001f, 0.0f, 0.5f)` — relit/écrit le uniform `parallaxScale` dans `def.mUniforms` (helper `findOrCreateFloatUniform`).
- **Boutons rapides** : 6 boutons sur une ligne `[0.01] [0.02] [0.04] [0.06] [0.08] [0.12]` qui poussent la valeur exacte. Couvrent 95 % des cas réels — l'utilisateur n'a pas besoin de bouger la souris pixel par pixel.
- **Presets nommés** : un combo `Combo("Preset", ...)` avec une dizaine d'entrées :
  - `Custom` (no-op, juste pour afficher l'état actuel)
  - `Stone wall (rough)` → 0.06
  - `Stone wall (carved)` → 0.04
  - `Brick / masonry` → 0.05
  - `Wood plank` → 0.03
  - `Wood log (deep)` → 0.06
  - `Fabric / tapestry` → 0.015
  - `Metal plate (subtle)` → 0.01
  - `Cobblestone` → 0.08
  - `Daedric / runic` → 0.05
  - `Cave wall` → 0.07

  Le combo détecte le preset actuel par valeur (snap à ±0.005) pour rester cohérent quand on rouvre l'éditeur.

- **Auto-detect** : un bouton `Auto-detect from texture name` qui matche `mDiffuseBasename` contre une liste de patterns :
  - `stone`, `rock`, `boulder` → 0.06
  - `brick`, `masonry` → 0.05
  - `wood`, `plank`, `log` → 0.04
  - `fabric`, `cloth`, `rug`, `tapestry` → 0.015
  - `metal`, `iron`, `steel` → 0.01
  - `dirt`, `mud`, `cave` → 0.07
  - `daedric`, `dwemer` → 0.05
  - Pas de match → 0.04 (default settings).
  Logique implémentée par une simple table `std::array<{const char*, float}, N>` parcourue avec `containsCI`.

- **Indicateur visuel** : sous le slider, une ligne d'état :
  - Si `hint.mHasHeightmap` : `ImGui::TextColored(green, "✓ heightmap detected — parallax effective")`
  - Sinon : `ImGui::TextColored(orange, "⚠ no heightmap on this slot — parallaxScale is a no-op (set Auto-use object normal maps + provide _nh.dds)")`
  - Coloration directe depuis `mHasHeightInNormalAlpha` calculé au probe time.

- **Lien vers POM** : un bouton `Configure POM (global)…` qui ouvre la pane ShaderSettings au focus. Pas de flag dédié — un simple `ImGui::SetWindowFocus("Shader Settings")`. ShaderSettings expose déjà `mParallaxOcclusion` + `mParallaxOcclusionSamples`, on n'a pas besoin de les répliquer per-slot.

**Helper interne** dans `materialeditor.cpp` (anonymous namespace) :
```cpp
float* findFloatUniform(MaterialDef&, const std::string& name);     // existing pattern
void   setFloatUniform(MaterialDef&, const std::string& name, float);
```
Évite la duplication du code `for (auto& u : def.mUniforms) ... std::holds_alternative<float> ...` qu'on retrouverait sinon à chaque preset click.

**Câblage côté EntityInspector** (`apps/openmw/mwgui/entityinspector.cpp:683-690`) : passer `ParallaxHint{slot->mHasHeightInNormalAlpha, basenamePath(slot->mDiffuse)}` à `drawMaterialDefInline`. La fenêtre Materials standalone (`MaterialEditor::draw`) appelle sans hint — le bloc parallax se rabat sur le slider seul, sans indicateur (pas d'info dispo).

### 2. Édition terrain dans EntityInspector

**Fichier :** `apps/openmw/mwgui/entityinspector.{cpp,hpp}` + petit accessor sur `Resource::SceneManager`/`Terrain`.

**Modèle d'interaction** : un nouveau mode "Pick terrain" parallèle au "Pick from world" existant (`entityinspector.cpp:209-225`). Quand actif, le clic dans le monde fait un raycast vers le terrain au lieu d'un raycast objet, retourne `(worldspace, cellX, cellY)` + la liste des layers résolus à ce point.

**État ajouté à `EntityInspector`** (.hpp) :
```cpp
bool mPickTerrainMode = false;
struct TerrainPick {
    bool valid = false;
    std::string mWorldspace;
    int mCellX = 0;
    int mCellY = 0;
    osg::Vec3f mWorldPos;
    std::vector<std::string> mLayerTextures; // diffuse paths in resolution order
};
TerrainPick mTerrainPick;
```

**Récupération des layers** : la classe `Terrain::World` (header `components/terrain/world.hpp`) expose `getStorage()` → `Terrain::Storage`. Storage a déjà une méthode pour résoudre les layers à un (cellX, cellY). À défaut on ajoute un wrapper :
```cpp
// components/terrain/storage.hpp (nouveau)
virtual void getCellLayerTextures(int cellX, int cellY,
    std::vector<std::string>& outTextures) = 0;
```
implémenté dans `terrain::ESMTerrain::getCellLayerTextures` qui lit déjà `mLand` pour les blendmaps (pattern existant dans `getBlendmaps`).

**UI nouvelle section** dans `EntityInspector::draw` (insérée après le bloc Material entité, autour de `entityinspector.cpp:660`) :

- Bouton `Pick terrain` (toggle similaire à `Pick from world`).
- Quand `mTerrainPick.valid` :
  - Affiche `Worldspace: <ws>  |  Cell: (<x>, <y>)`
  - Liste les layers : `Layer 0: <texture0>`, `Layer 1: <texture1>`, …
  - Query registry : `registry->matchTerrain(ws, cellX, cellY)`.
  - Si match : `drawMaterialDefInline(*matched)` (l'éditeur uniforms est identique pour entités/terrain — c'est juste un MaterialDef).
  - Si pas de match : un picker simplifié à deux portées :
    - `Per cell` → produit un `TerrainRule{worldspace, cells:[{x,y}]}`
    - `Per worldspace` → `TerrainRule{worldspace, cells:[]}` (cells vide = wildcard à interpréter dans `matchTerrain`, à confirmer en lisant le matcher)

  - Bouton `Create terrain override` qui appelle un nouvel helper :
    ```cpp
    // materialeditor.hpp
    Material::MaterialDef makeTerrainOverride(
        const std::string& worldspace, int cellX, int cellY,
        bool perWorldspace, float parallaxScaleSeed);
    ```
    Crée un `MaterialDef` avec **uniquement** un `TerrainRule` (pas de `MatchRule`), `mName = "terrain__" + worldspace + "__" + x + "_" + y`, `priority = 100`, parallaxScale seed.

- Save / delete reprennent le flow `writeEntityOverrideYaml` mais avec un fichier nommé `terrain_<worldspace>_<x>_<y>.yaml` (ou `terrain_<worldspace>.yaml` pour le wildcard). Le writer existant n'émet pas le bloc `terrain:` du schéma — à compléter (parser à `materialregistry.cpp:99-115` le sait déjà lire) :

  ```yaml
  - name: 'terrain__morrowind__-3_-10'
    priority: 100
    match:
      terrain:
        worldspace: 'morrowind'
        cells: [{ x: -3, y: -10 }]
    uniforms:
      - { name: parallaxScale, type: float, value: 0.04 }
  ```

  Patch : `writeEntityOverrideYaml` devient `writeMaterialDefsYaml` (renommage), et émet `terrain:` quand `def->mTerrainRules` est non-vide en plus de `match.any` quand `def->mRules` est non-vide.

- Le bouton existant `Save as YAML override` du panneau entité reste pour les entités. Le mode terrain a son propre `Save terrain override` à côté de la Pick.

**Raycast terrain** : on réutilise `RenderingManager::castCameraToViewportRay` (`entityinspector.cpp:183`). Il retourne déjà la position du hit. Pour distinguer terrain vs objet : tester `rayRes.mHitObject.isEmpty()` — si vide, c'est du terrain (le raycast inclut déjà le mesh terrain dans son DCC). Le worldspace vient de `rayRes.mHitPos.x/y` divisé par `Constants::CellSizeInUnits` pour les coordonnées de cellule, et de `getWorld()->getCurrentCellName()` ou `mPlayer.getCell()->getCell()->getCellId().mWorldspace` pour la chaîne worldspace.

### 3. Intégration commune

- Pas de nouveau fichier source. Tout se passe dans `materialeditor.{cpp,hpp}` (helper parallax, helper terrain override, writer générique) et `entityinspector.{cpp,hpp}` (UI terrain pick + scope picker).
- `Material::Registry::add()` (déjà ajouté à la phase précédente) sert aussi pour le terrain override — même mécanique.
- `recreateShaders(root)` est obligatoire après création/suppression d'un terrain override : `terrain::material.cpp:284` lit le registry au moment de bâtir les passes ; sans une re-passe ShaderVisitor + terrain rebuild les chunks live ne voient pas le nouveau MaterialDef.

## Fichiers modifiés

- `apps/openmw/mwgui/materialeditor.{cpp,hpp}` — bloc parallax avec presets/auto-detect, hint-driven indicator, helper float-uniform, `makeTerrainOverride`, writer renommé + extension `terrain:`.
- `apps/openmw/mwgui/entityinspector.{cpp,hpp}` — `mPickTerrainMode`, `mTerrainPick` state, UI terrain section, propagation du `ParallaxHint` au editor inline.
- `components/terrain/storage.hpp` + `components/terrain/esmterrain.cpp` — accessor `getCellLayerTextures` (~30 LOC, lit la même `mLand` que `getBlendmaps`).
- `apps/openmw/mwgui/imguioverlay.cpp:281-301` — élargir le pick-event handler pour reconnaître `mPickTerrainMode` en plus de `isPickMode()`.

## Vérification

1. **Build** Bullet et Jolt — `ninja openmw` dans `build-jolt-test`, `make` dans `build`.
2. **Helper parallax** : ouvrir un mur en pierre vanilla → Material section → vérifier que le combo Preset propose "Stone wall (rough)" et que cliquer pousse la bonne valeur ; tester un slot sans heightmap (par ex. une porte sans `_nh.dds`) → vérifier que l'indicateur orange apparaît et que le slider est barré ou disabled si on veut être strict.
3. **Auto-detect** : pick une `daedric_dagger` → cliquer Auto-detect → valeur passe à 0.05.
4. **Terrain pick** : ouvrir l'overlay en extérieur → Pick terrain → cliquer le sol → la liste des layers de la cellule s'affiche, les coordonnées correspondent au cell visible (`tcl + getpos`).
5. **Terrain override** : Create override (per cell), modifier `parallaxScale` → vérifier que **seul** ce chunk change (les voisins gardent la valeur globale).
6. **Wildcard worldspace** : Create override (per worldspace) sur une cellule de Solstheim → vérifier que toutes les cellules Solstheim chargées prennent la valeur, mais pas Morrowind.
7. **Save / reload** : sauver `terrain_morrowind_-3_-10.yaml`, vérifier que ré-ouvrir le jeu reconstruit l'override correctement (parser multi-defs déjà OK depuis la phase précédente).
