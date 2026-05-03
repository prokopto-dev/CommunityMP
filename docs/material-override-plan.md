# Plan — système de matériels custom & overrides mesh/terrain

> **Couplé avec `imgui-overlay-plan.md` Phase 8** : ce plan fournit le backend (registry YAML, hot-reload, matching). L'inspector ImGui (Phase 8b) consomme `MaterialRegistry` comme UI d'authoring + preview. Implémenter Phase 1 ici avant Phase 8b côté ImGui.

## Contexte (audit du code existant)

### Comment OpenMW génère les matériels aujourd'hui

**Mesh (NIF) → StateSet :**
- `components/nifosg/nifloader.cpp:670` nomme chaque osg::Node avec le nom du `nifNode->mName` du NIF, et stocke `recordIndex` en `setUserValue` (ligne 682). Les texprops NIF Bethesda posent aussi `setUserValue("shaderPrefix", ...)` (lignes 2485, 2500, 2522, 2548) — c'est précisément le hook qu'utilise déjà le shader visitor pour choisir un shader autre que `objects`.
- `components/resource/scenemanager.cpp:968 getTemplate()` charge le NIF, applique un `Shader::ShaderVisitor` (ligne 994), puis met l'arbre en cache. Le `getInstance()` (ligne 1046) clone-shallow le template et l'attache à un `PositionAttitudeTransform` ; les StateSets sont donc **partagés entre instances par défaut** (cf `SHARE_DUPLICATE_STATE` ligne 1003).
- `components/shader/shadervisitor.cpp:347 applyStateSet()` parse les textures fixed-function du NIF, peuple `ShaderRequirements`, puis `createProgram()` (ligne 631) construit le `defineMap` et résout le programme final via `mShaderManager.getProgram(shaderPrefix, defineMap, …)` ligne 791. Le `shaderPrefix` lu en ligne 788 est ce que l'on veut overrider.
- Le seul système d'override existant aujourd'hui : `components/shader/shadervisitor.cpp:48 parallaxOverrides()` — un `Settings::shaders().mParallaxOverrides` au format `pattern1=0.08;pattern2=0.02;…`, matché en sous-string case-insensitive contre le filename de la diffuse (ligne 79 `lookupParallaxOverride`). C'est le précédent **dont on s'inspire directement**.

**Terrain (heightmap) → StateSet :**
- `components/terrain/material.cpp:266 createPasses()` construit, par layer ESM, un `osg::StateSet` avec : diffuseMap (unit 0), blendMap (unit 1), normalMap (unit 2) optionnel, procedural bump (unit 3), puis assigne le programme via `shaderManager.getProgram("terrain", defineMap)` (ligne 401) ou `getTessellationProgram("core/terrain", defineMap)` (ligne 387). Le defineMap (lignes 357-368) drive normalMap/blendMap/specularMap/parallax/writeNormals/reconstructNormalZ/terrainProceduralBump.
- `components/terrain/chunkmanager.cpp:142 getChunk()` est appelé avec un `chunkCenter` (Vec2f en cell units) et un `chunkSize` (1, 2, 4…). C'est le seul endroit où l'on connaît la position monde du chunk au moment de la création des passes. Les chunks ne portent **ni nom ni userValue** aujourd'hui (`components/terrain/world.cpp:30` ne nomme que le root).

**Shader manager — assemblage des templates :**
- `components/shader/shadermanager.cpp:518` cherche le template (`mShaderTemplates`), le charge depuis `mPath` (= `files/shaders/compatibility` ou `core` selon le profil GL), inline les `#include`, applique les `@define`, et cache le résultat par `(templateName, defineMap)`.
- Le hot-reload existe déjà — `HotReloadManager` (lignes 186-290) re-watch les fichiers source touchés depuis `mLastAutoRecompileTime` et fait `setShaderSource` sur le shader OSG live, en stoppant le viewer threading le temps du swap. Activable via `setHotReloadEnabled(true)` ; déjà exposé en Lua (`apps/openmw/mwlua/debugbindings.cpp:72`).
- Les passes post-process `.omwfx` sont un autre pipeline (`components/fx/`) — **hors scope** ici, on parle des matériels d'objets/terrain, pas des effets écran.

### Constat
Les hooks naturels existent. Le shader manager est paramétré par `(templateName, defineMap, programTemplate)` — pour overrider un matériel il suffit, à la création du StateSet, de **substituer ces trois valeurs** au lieu des valeurs par défaut. Aucun fork du shader manager nécessaire.

## Cible

L'utilisateur veut :

1. Définir des matériels en YAML (yaml-cpp est déjà linké, cf `CMakeLists.txt:351-359`) dans un dossier `data/materials/*.yaml` (lu via VFS).
2. Matcher par : nom de mesh NIF, nom de nœud à l'intérieur du NIF, sous-string sur path de texture diffuse, RefId Morrowind, ou cellule terrain (worldspace+cellX+cellY).
3. Overrider : shader vert/frag (par templateName), uniforms (typed), textures (par unit), render states (blend func, depth func, cull mode).
4. Hot-reload des `.yaml` pendant la session — cohérent avec le hot-reload shader existant.

### Exemple de fichier matériel cible

```yaml
# data/materials/glow_lantern.yaml
name: glow_lantern
match:
  any:
    - mesh: "f/furn_lantern_*.nif"        # glob sur path NIF
    - texture: "tx_lantern_glow"           # sous-string sur diffuse
    - record_id: "furn_lantern_01"         # exact match RefId
shader:
  vertex: objects                          # template name (compat/core auto-resolved)
  fragment: emissive_pulse                 # nouveau template à créer dans files/shaders/compatibility/
defines:
  emissivePulseSpeed: "2.0"
  parallax: "0"
uniforms:
  - { name: uPulseColor,  type: vec3,  value: [1.0, 0.6, 0.2] }
  - { name: uPulseAmp,    type: float, value: 0.3 }
textures:
  - { unit: 4, path: "textures/fx/noise_lf.dds", wrap: repeat, filter: linear }
state:
  blend: alpha          # off | alpha | additive | premult
  depth_test: lequal    # off | less | lequal | equal
  depth_write: true
  cull: back            # off | back | front
priority: 100           # plus haut = écrase les règles plus basses
```

```yaml
# data/materials/balmora_terrain.yaml
name: balmora_terrain_wet
match:
  terrain:
    worldspace: morrowind
    cells: [{ x: -3, y: -10 }, { x: -3, y: -9 }, { x: -2, y: -10 }]
shader:
  vertex: terrain
  fragment: terrain_wet     # nouveau template
defines:
  wetnessStrength: "0.6"
priority: 50
```

## Architecture proposée

### Composants

- **`components/material/`** (nouveau) — registry, parser YAML, matcher.
  - `material.hpp/cpp` : struct `MaterialDef` (defines, uniforms, textures, state, shader templates, priority, match rules).
  - `materialregistry.hpp/cpp` : singleton-via-ResourceSystem. Charge tous les `data/materials/**/*.yaml` au démarrage via `VFS::Manager`, watch leur mtime pour hot-reload. Fournit :
    - `const MaterialDef* matchMesh(const std::string& meshPath, const std::string& nodeName, const std::string& diffuseFilename, std::optional<std::string> recordId);`
    - `const MaterialDef* matchTerrain(ESM::RefId worldspace, int cellX, int cellY);`
  - `materialapplier.hpp/cpp` : prend un `MaterialDef` + `osg::StateSet*` cible et applique l'override (résolution shader via `ShaderManager::getProgram`, push uniforms, set BlendFunc/Depth/CullFace).
- **CMake** : ajouter `material` au bloc `add_component_dir` de `components/CMakeLists.txt:62`. Linker `yaml-cpp` (déjà dispo).
- **VFS path convention** : `data/materials/` lu via `mVFS->getRecursiveDirectoryIterator("materials/")`.

### Hooks d'interception

**Hook mesh — `components/shader/shadervisitor.cpp:787-792`**

```cpp
std::string shaderPrefix;
if (!node.getUserValue("shaderPrefix", shaderPrefix))
    shaderPrefix = mDefaultShaderPrefix;
auto program = mShaderManager.getProgram(shaderPrefix, defineMap, mProgramTemplate);
```

Insérer juste avant le `getProgram`, un appel au `MaterialRegistry::matchMesh` avec :
- `meshPath` = path NIF du template courant (passé via le `ShaderVisitor` constructeur, déjà tracé en `getTemplate`)
- `nodeName` = `node.getName()` (rempli par `nifloader.cpp:670`)
- `diffuseFilename` = `diffuseMap->getImage(0)->getFileName()` (déjà calculé pour le parallax override ligne 412)
- `recordId` : nécessite plumbing — le `ShaderVisitor` ne le connaît pas. **Phase 2** : poser un `setUserValue("refId", ptr.getCellRef().getRefId().toString())` dans `apps/openmw/mwrender/objects.cpp:60` (juste après `addUserObject(new PtrHolder)`), et le lire via `node.getUserValue("refId", …)` dans le visitor. Pour le MVP Phase 1 on s'en passe.

Si match, fusionner `defineMap` avec `MaterialDef::defines`, swap `shaderPrefix` par `MaterialDef::shader.fragment`, puis appliquer uniforms/textures/state. Le `ShaderVisitor::AddedState` (lignes 100-150) est déjà conçu pour tracer ce qui a été ajouté ; étendre proprement pour le re-apply lors du shader recreate.

**Hook terrain — `components/terrain/material.cpp:266 createPasses()`**

`createPasses` ne reçoit aujourd'hui ni `chunkCenter` ni `worldspace` — il faut **étendre la signature** avec `(ESM::RefId worldspace, osg::Vec2f chunkCenter)`. Mettre à jour les deux callers dans `components/terrain/chunkmanager.cpp:294` et `:488`. Dans le terrain layer loop, calculer `cellX = floor(chunkCenter.x())`, `cellY = floor(chunkCenter.y())`, appeler `MaterialRegistry::matchTerrain(worldspace, cellX, cellY)`. Si match, override le `shaderManager.getProgram("terrain", defineMap)` ligne 401 par le matériel résolu.

Note : le composite map (chunkSize > `mCompositeMapLevel`) bake les passes dans un RTT 2D. Pour les overrides de chunks loin, deux options :
- **MVP** : ne supporter les overrides terrain QUE sur les chunks proches non-composités (`createPasses(forCompositeMap=false)`). Documenter la limite.
- **Phase 4** : hook aussi le `createCompositeMapGeometry` (`chunkmanager.cpp:201`) pour que le bake intègre l'override.

### Système de matching

Trois niveaux, par ordre de complexité :

| Niveau | Match types | Coût | Phase |
|---|---|---|---|
| 1 | exact string + sous-string + suffix | O(N) règles, négligeable | MVP |
| 2 | glob `*` `?` (translaté en regex simple) | O(N) compile+match | Phase 2 |
| 3 | regex pure `^/.*\.nif$` | O(N) match | Phase 3 si demandé |

Précédence : tri par `priority` desc à la sélection. Si égalité, premier match dans l'ordre de chargement (alphabétique du filename). En cas de matches multiples, on prend juste **le top 1** ; on ne tente pas de "merger" deux matériels (trop ambigu pour les uniforms typés).

Cache : `MaterialRegistry::matchMesh` retourne via une `std::unordered_map<MeshKey, const MaterialDef*>` keyed sur `(meshPath, nodeName, diffuseHash)`. Invalidée au hot-reload.

## Phases d'implémentation

### Phase 1 — MVP : override mesh par sous-string sur diffuse (~6 h)

**But** : démontrer le pipeline end-to-end. Choisir une mesh visible (ex `f/furn_lantern_01.nif`), créer un matériel YAML qui swap son fragment shader vers une variante avec un uniform de couleur, voir le résultat in-game.

Toucher :
- `components/material/` : créer `materialdef.{hpp,cpp}`, `materialregistry.{hpp,cpp}`, `materialapplier.{hpp,cpp}`. Pas encore d'API matchTerrain. Pas encore de hot-reload (les .yaml sont parsés au démarrage uniquement).
- `components/CMakeLists.txt:62` : ajouter `material`.
- `components/resource/scenemanager.{hpp,cpp}` : créer/owner un `Material::Registry` au constructeur du `SceneManager`, exposer un `getMaterialRegistry()`.
- `components/shader/shadervisitor.{hpp,cpp}` :
  - ctor étendu pour accepter `Material::Registry*` (passé par `SceneManager::createShaderVisitor` ligne 1214).
  - dans `createProgram` (ligne 631), juste après le calcul du `defineMap` et de la `diffuseFilename` (déjà disponible ligne 412), appeler `registry->matchMesh(...)`. Si hit, override `shaderPrefix` et merge `defineMap`. Pousser uniforms/textures/state sur `writableStateSet` en utilisant le pattern déjà en place pour `parallaxScale` (ligne 802-808) — en `OVERRIDE` flag.
- Créer un fragment shader exemple `files/shaders/compatibility/emissive_pulse.frag` (lien vers `objects.frag` avec une dérivation simple).

**Critère** : un fichier `data/materials/test_lantern.yaml` qui matche `tx_lantern` rend les lanternes avec une teinte rouge animée par `osg_SimulationTime`. Pas de crash si le YAML est malformé (log + skip). Pas de régression sur les autres meshes.

**Risques** :
- Le `SceneManager::shareState` (ligne 1008) déduplique les StateSets — un override appliqué sur un template partagé entre 50 lanternes affecte les 50 d'un coup. C'est OK pour le MVP (c'est même souhaité), mais pose problème si on veut overrider une seule instance par RefId. Phase 2.
- Le shader visitor est appelé sur le template **avant** le clone. Si on veut un override par-instance, il faut déplacer l'application au niveau de `getInstance` — Phase 2.

### Phase 2 — RefId + glob matching + override per-instance (~6 h)

- Plumber le `refId` : `apps/openmw/mwrender/objects.cpp:60` ajoute `insert->setUserValue("refId", ptr.getCellRef().getRefId().toString())`. Le `MaterialApplier` doit alors tourner **post-clone**, dans une passe légère type `ShaderVisitor` mais opérant sur l'instance, pas le template. Concrètement : un nouveau `Material::OverrideVisitor` lancé depuis `Objects::insertModel` après `insertBegin`.
- Glob matching : impl simple `glob_to_regex` (5 lignes).
- Hot-reload : étendre `Material::Registry::update(viewer)` sur le pattern de `Shader::HotReloadManager` (`shadermanager.cpp:212`). Re-parser les .yaml modifiés, puis flush le cache de match et trigger un `triggerShaderReload()` sur le `ShaderManager` pour que les programmes soient ré-instanciés. Optionnel : un `materialRegistry->reload()` Lua bindable comme `triggerShaderReload`.

**Critère** : une règle `record_id: "furn_de_lantern_01_64"` n'affecte qu'une lanterne précise. Modifier le .yaml en cours de session ré-applique l'override sans restart. Latence ≤ 500 ms.

**Risques** :
- Le double-pass (ShaderVisitor au load + OverrideVisitor au insert) double le coût d'instanciation. Bencher sur Vivec qui charge 200+ meshes par cell. Si > 5 % de regression, fusionner les deux visiteurs.

### Phase 3 — Override terrain (~5 h)

- Étendre `Terrain::createPasses` pour recevoir `(ESM::RefId worldspace, osg::Vec2f chunkCenter)`. Patch les 2 callers `chunkmanager.cpp:294` et `:488`.
- Dans `material.cpp:401`, juste avant `getProgram("terrain", defineMap)`, query `MaterialRegistry::matchTerrain(worldspace, cellX, cellY)` ; si hit, swap le templateName et merge defines.
- Limiter au non-composite pour le MVP (skip dans la branche `forCompositeMap=true`). Documenter dans le YAML que les overrides terrain ne s'appliquent qu'au LOD0/LOD1.

**Critère** : `cells: [{ x: -3, y: -10 }]` rend Balmora avec un shader terrain personnalisé visible. Les cellules adjacentes restent vanilla.

**Risques** :
- Le terrain `mMultiPassRoot` (`chunkmanager.cpp:135`) impose un `RenderingHint::OPAQUE_BIN`. Les overrides custom doivent respecter — refuser `blend: alpha` côté terrain et logger un warning.
- Composite map exclu du MVP terrain : les chunks lointains restent vanilla → discontinuité visible à la frontière LOD. Acceptable pour Phase 3 ; corriger en Phase 5 si besoin.

### Phase 4 — Persistance saves + UI (~3 h)

- Persistance : aucune. Les overrides matériels sont **purement asset-side** (lus depuis `data/`), pas de state à sérialiser. Une save reload re-lit les YAML depuis le VFS — donc si l'utilisateur a modifié un matériel entre deux sessions, le save l'utilise tel quel. Documenter explicitement.
- UI debug : un panneau Lua minimal (`scripts/omw_aux/material_debug.lua`) qui liste les matériels chargés, leur nombre de matches actifs, et propose un bouton "reload all materials". Greffer sur le `debugbindings` existant.

**Critère** : `/materials list` (Lua console) affiche les 12 matériels chargés ; `/materials reload` re-parse sans restart.

### Phase 5 (optionnelle) — composite map terrain + multi-shader passes (~5 h)

- Hook `createCompositeMapGeometry` pour injecter l'override dans le bake RTT.
- Support de plusieurs passes par matériel (déjà naturel côté terrain via `passes`, à étendre côté mesh — un `MaterialDef` peut produire un vector de StateSets empilés).

## Risques / trade-offs

| Risque | Détail | Mitigation |
|---|---|---|
| Coût compilation shader | Chaque nouveau matériel = nouvelle entrée `(template, defineMap)` dans le cache du ShaderManager → nouvelle compile GLSL au premier draw. 30 matériels custom = 30 stalls visibles au premier load de cellule. | Précompiler au démarrage : itérer le registry, faire un `getProgram(...)` sec sur chaque matériel pendant le splash screen pour amortir. |
| Conflit avec `shaderPrefix` NIF (ex BS lighting) | Si un mesh BS pose `shaderPrefix=bs/lighting` (`nifloader.cpp:2522`) ET un matériel custom matche, qui gagne ? | Règle : le matériel YAML gagne **sauf si** le NIF a `bs/*` shaderPrefix ET pas d'override `force: true` dans le YAML. Documenter. |
| Cache invalidation au hot-reload | Modifier un .yaml force re-shader-compile + re-StateSet tout l'arbre. Sur Vivec ça stoppe le viewer threading 100-300 ms. | Acceptable pour le dev ; désactivable en release. Aligner sur la stratégie shader hot-reload existante. |
| Persistance save | Aucune — les overrides sont asset-side. Si l'utilisateur supprime un matériel entre deux sessions, les meshes redeviennent vanilla en silence. | Documenter explicitement, fournir un log au load de save listant les matériels actifs. |
| Précédence multi-match ambiguë | Deux règles avec `priority` égal qui matchent → comportement non-déterministe à l'œil nu. | Tri stable secondaire par filename alphabétique. Logger en `Debug::Warning` au load si on détecte deux règles à priorité égale qui peuvent matcher le même target. |
| `SHARE_DUPLICATE_STATE` | Le SceneManager partage les StateSets entre instances par défaut (ligne 1003). Override par-instance Phase 2 doit forcer un copy-on-write. | Le `OverrideVisitor` Phase 2 fait un `setStateSet(new StateSet(*old, SHALLOW_COPY))` avant de modifier, comme `getWritableStateSet` (`shadervisitor.cpp:277`). |

## Estimation totale

- Phase 1 : ~6 h (MVP démontrable)
- Phase 2 : ~6 h (RefId + glob + hot-reload)
- Phase 3 : ~5 h (terrain non-composite)
- Phase 4 : ~3 h (UI/Lua debug)
- Phase 5 : ~5 h (composite map + multi-pass) — optionnel
- **Total minimum** : ~17 h pour un système solide mesh + terrain hot-reloadable.
- **Total avec extras** : ~25 h.

Phases 1-2-3 sont indépendantes des Phase 4-5. Phase 1 doit shipper d'abord pour valider l'architecture du registry avant d'investir dans le terrain et la persistance.

## Fichiers à toucher (sommaire)

**Nouveaux** :
- `components/material/materialdef.{hpp,cpp}`
- `components/material/materialregistry.{hpp,cpp}`
- `components/material/materialapplier.{hpp,cpp}`
- `files/data/materials/example.yaml`
- `files/shaders/compatibility/emissive_pulse.frag` (exemple Phase 1)

**Modifiés** :
- `components/CMakeLists.txt:62`
- `components/resource/scenemanager.{hpp,cpp}` : owner du registry, passe-plat vers le visitor.
- `components/shader/shadervisitor.{hpp,cpp}` : hook ligne 787-792, ctor étendu.
- `components/terrain/material.{hpp,cpp}` : signature `createPasses` étendue, hook ligne 401.
- `components/terrain/chunkmanager.cpp:294,488` : passer worldspace+chunkCenter.
- `apps/openmw/mwrender/objects.cpp:60` (Phase 2) : poser `refId` sur le node.
- `apps/openmw/mwlua/debugbindings.cpp` (Phase 4) : exposer `materials.reload()`.
