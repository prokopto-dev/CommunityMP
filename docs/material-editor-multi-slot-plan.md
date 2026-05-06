# Éditeur de matériaux multi-slots dans l'overlay ImGui

## Contexte

Le parallax fonctionne. L'éditeur de matériaux actuel (Material section dans Entity Inspector) ne montre qu'**un seul** matériel : `MaterialProbe` (`apps/openmw/mwgui/entityinspector.cpp:81-172`) court-circuite dès qu'il trouve une StateSet avec un diffuse non vide (`mFound=true` à la ligne 144). Pour un mesh complexe (NPC habillé, créature, bâtiment multi-textures), seul le premier sub-mesh est éditable.

L'objectif : énumérer **tous** les slots matériels du mesh sélectionné, et permettre de créer un override avec une portée au choix — par child (`nodeName`), par texture (`textureSubstr`), par refId, ou par mesh path. Tout reste dans l'éditeur ImGui existant ; pas de fenêtre séparée.

Le backend (registry, ShaderVisitor, MaterialApplier, schéma `MaterialDef`) couvre déjà ces dimensions de match — il n'y a pas besoin de modifier le pipeline d'application. Seuls le parser YAML, l'API du registry, et la couche UI bougent.

## Approche

### 1. Énumérateur multi-slots
**Fichier :** `apps/openmw/mwgui/materialeditor.cpp` / `materialeditor.hpp`

Nouveau type public dans `materialeditor.hpp` (à côté de `drawMaterialDefInline`) :
```cpp
struct MaterialSlot {
    const osg::StateSet* mStateSetKey;  // dedupe seulement, jamais déréférencé après collecte
    std::string mNodePath;              // "/Bip01/Hair/Tri Hair"
    std::string mNodeName;              // dernier segment non vide
    std::string mDrawableName;          // pour les fallbacks anonymes
    std::string mDiffuse, mNormal, mSpecular, mBump;
    bool mHasHeightInNormalAlpha = false;
    bool mAnonymous = false;            // true → port. "Per child" désactivée
};
std::vector<MaterialSlot> collectMaterialSlots(osg::Node& root);
```

Implémentation : un `osg::NodeVisitor` qui dérive de `MaterialProbe` mais **sans** le short-circuit `mFound`. Maintient un `std::unordered_set<const osg::StateSet*>` pour dédupliquer (les StateSets sont partagées entre drawables d'un même clone NIF). Pile manuelle pour `mNodePath` (push sur `apply(Node&)`, pop après `traverse`). Réutilise le bloc de classification de textures à `entityinspector.cpp:118-167` tel quel. Émet aussi les slots **sans diffuse** si au moins un normal/spec/bump est présent (FX shells).

### 2. UI : sélecteur de slot dans l'Entity Inspector
**Fichier :** `apps/openmw/mwgui/entityinspector.hpp` / `entityinspector.cpp:432-510`

Ajout dans `EntityInspector` (après `mSelected`) :
```cpp
int mSelectedSlot = -1;
std::string mSelectedSlotKey;  // "<nodePath>|<diffuse>" — re-résolu chaque frame
```

Remplacer l'instantiation `MaterialProbe probe` (~ligne 432) par `auto slots = collectMaterialSlots(*base);`. Rendre la liste via `BeginTable("Slots", 2)` — colonnes : Node | Diffuse basename. Sur clic, stocker `mSelectedSlotKey`. **Chaque frame**, re-résoudre l'index via la clef avant fallback à l'ancien index — même pattern de stabilité que la validation des Ptrs à `entityinspector.cpp:314-327`.

Le bloc de diagnostic textures à `:439-464` devient per-slot sélectionné.

### 3. Résolution d'override + scope picker
**Fichier :** `apps/openmw/mwgui/entityinspector.cpp:467-510`

Récupérer le mesh basename via `mSelected.getClass().getModel(mSelected)` (déjà disponible, voir `apps/openmw/mwworld/class.cpp`). Passer ce basename au lieu de `""` dans l'appel `registry->matchMesh(meshPath, slot.mNodeName, slot.mDiffuse, refId)` à la ligne 468.

Filtrer les MaterialDefs purement terrain (`!mTerrainRules.empty() && mRules.empty()`) en amont.

Si match : `drawMaterialDefInline(*matched)` (existant). Si `nullptr` : afficher un picker avec **4 cases** (les 4 portées validées). La case "Per child" est désactivée si `slot.mAnonymous`. Bouton "Create override" → appel à un nouvel helper :

```cpp
// dans materialeditor.hpp
Material::MaterialDef makeMaterialDefForSlot(
    const MaterialSlot& slot, uint32_t scopeFlags,
    const std::string& refId, const std::string& meshBasename);
```
Implémentation dans `materialeditor.cpp` : construit **une `MatchRule` par flag** (toutes OR ensembles dans `mRules`), `mName = sanitise(refId) + "__" + sanitise(slotKey)`, `mPriority = 100`, ajoute un uniform de démarrage (`parallaxScale = settings.parallaxScale`).

Le caller pousse ensuite via la nouvelle API du registry (étape 4).

### 4. `Registry::add()`
**Fichier :** `components/material/materialregistry.hpp:73`, `materialregistry.cpp`

Nouvelle méthode publique :
```cpp
MaterialDef* add(MaterialDef def);
```
Implémentation : déduplique par `mName` (même logique qu'à `materialregistry.cpp:225-227`), `push_back` un `unique_ptr`, re-trie par priorité (même `stable_sort` qu'à `:231-232`), retourne le pointeur stable. Ce raccourci évite le round-trip disque à chaque création, tout en gardant le YAML save comme snapshot persistant explicite.

### 5. YAML multi-defs : parser + writer
**Fichier :** `components/material/materialregistry.cpp:209-240` (parser) ; `apps/openmw/mwgui/entityinspector.cpp:512-655` → déplacer vers `materialeditor.cpp` (writer)

**Parser** : dans `loadFile`, après `YAML::Load`, détecter le format :
- Si la racine contient un nœud `materials:` qui est une `Sequence`, itérer et appeler `parseMaterial(node, fsPath)` sur chaque élément. Dédupliquer chacun par nom dans `mMaterials`.
- Sinon, comportement actuel (un seul `parseMaterial` sur la racine) — rétro-compatible avec les fichiers existants.

**Writer** : nouvelle fonction libre dans `materialeditor.cpp` :
```cpp
void writeEntityOverrideYaml(
    const std::filesystem::path& path,
    const std::string& refId,
    const std::vector<const Material::MaterialDef*>& defs);
```
- Top-level : `materials: [...]`.
- Pour chaque `def` : émet `name`, `priority`, `match.any` avec **toutes** les clefs (`mesh`, `node`, `texture`, `record_id`) — actuellement le writer à `entityinspector.cpp:583-587` ne sort que `record_id`, à compléter (le parser à `materialregistry.cpp:74-81` les lit déjà).
- Réutilise la sérialisation de defines + uniforms typés (`std::visit`) du bloc existant à `:620-640`.

**Logique de save** : sur clic "Save as YAML override" → walker `registry` et collecter tous les `MaterialDef*` dont `mName` commence par `sanitise(refId) + "__"` (plus l'ancien nom legacy `<refId>_override` pour rétro-compat) ; passer le vecteur à `writeEntityOverrideYaml`. Si zéro def restante, `std::filesystem::remove` du fichier.

**Logique de delete-slot** : retire la def du registry (`removeByName`), ré-écrit le fichier avec les survivantes.

### 6. Discipline du re-shading
**Fichier :** `apps/openmw/mwgui/entityinspector.cpp` (les callsites de mutation)

Règle pragmatique : depuis l'Entity Inspector, **toujours** appeler `sceneMgr->recreateShaders(root)` après une mutation (la passe complète coûte peu pour un click utilisateur, et toute édition peut affecter la résolution de match). Garder `triggerShaderReload()` seul comme défaut pour la fenêtre Materials standalone qui boucle sur les mêmes defs.

Concrètement : remplacer `triggerShaderReload()` par `recreateShaders(root)` aux callsites Entity Inspector — `:497` (Delete override), `:508` (édition inline), `:650` (Save). Pattern déjà en place à `materialeditor.cpp:387-392`.

### 7. Cas particuliers
- **Slot sans diffuse** : émis quand même si normal/spec/bump présent. `Per texture` désactivée (substring vide).
- **Node anonyme** : `mAnonymous=true`, label "Drawable #N", checkbox `Per child` grisée.
- **MaterialDef terrain** : filtrée avant rendu dans la résolution per-slot.
- **Stabilité de l'identité du slot** : re-résolution par `mSelectedSlotKey` chaque frame ; recharge de cellule = la clef survit ou la sélection retombe à -1.
- **Équipement d'acteur** : `getModel()` ne retourne que le NIF de base. Documenter que `Per mesh path` ne couvre pas les pièces équipées attachées (helmet/weapon) — limitation MVP acceptable.

## Fichiers modifiés

- `apps/openmw/mwgui/entityinspector.cpp` (+ `.hpp`) — sélecteur de slot, scope picker, refonte du save/delete
- `apps/openmw/mwgui/materialeditor.cpp` (+ `.hpp`) — `collectMaterialSlots`, `makeMaterialDefForSlot`, `writeEntityOverrideYaml`
- `components/material/materialregistry.cpp` (+ `.hpp`) — `Registry::add()`, parser multi-defs

## Fonctions/utilitaires existants réutilisés

- `drawMaterialDefInline` (`materialeditor.cpp:50-296`) — éditeur de uniforms / defines / bump matrix, inchangé
- `parseMaterial` (`materialregistry.cpp` privé) — parser d'un seul MaterialDef, appelé en boucle dans le nouveau path
- `MaterialApplier::pushUniforms` / `mergeDefines` (`components/material/materialapplier.cpp`) — pas touchés
- `ShaderVisitor` apply seam (`shadervisitor.cpp:875-884`) — pas touché ; les nouvelles règles sont juste plus fines
- `SceneManager::recreateShaders` / `getShaderManager().triggerShaderReload` — réutilisés tels quels
- Validation Ptr-vs-cell (`entityinspector.cpp:314-327`) — modèle pour la stabilité de la clef de slot

## Vérification

1. **Build** : `cmake --build build -j` (chaîne actuelle ; le projet compile avec les modifs des 5 derniers commits).
2. **Lancement manuel** (utilisateur uniquement, pas auto-launch) : ouvrir l'overlay, sélectionner un mesh multi-matériaux (un NPC en armure, un Daedric shrine), vérifier que la table de slots liste tous les sub-meshes distincts, dans l'ordre depth-first stable.
3. **Override per-child** : créer un override `Per child` sur un slot, modifier `parallaxScale`, vérifier que seul ce sub-mesh change visuellement (les autres slots du même mesh restent inchangés).
4. **Override per-texture** : créer un override `Per texture` sur le diffuse d'un mur partagé, vérifier la propagation à toutes les instances qui partagent cette texture.
5. **Save YAML** : sauver, rouvrir le fichier `<userdata>/data/materials/<refId>.yaml`, vérifier la racine `materials: [...]` avec N entrées, chaque rule.any contenant la bonne clef (`node`, `texture`, `record_id`, `mesh`).
6. **Reload** : redémarrer le jeu, vérifier que les overrides persistent et appliquent correctement à toutes les portées (régression test du parser multi-defs).
7. **Delete slot** : supprimer un override depuis l'UI, vérifier que le YAML est ré-écrit sans cette entrée et que le fichier disparaît si zéro override restant.
8. **Rétro-compat** : un fichier YAML mono-def existant doit toujours se charger (path legacy de `loadFile`).
