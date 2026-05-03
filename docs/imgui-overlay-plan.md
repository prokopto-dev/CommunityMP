# Plan — menu ImGui « entity inspector » en overlay OpenMW

## Objectif fonctionnel
Toggle (F1 par défaut) → fenêtre flottante au-dessus du jeu permettant :
1. Lister / sélectionner / inspecter / éditer les entités proches.
2. Spawner un nouveau ref (mesh static, activator, container, light, misc, door) à la position du joueur, sous le crosshair, ou à des coords explicites.
3. (Phase 6) Spawner des objets en physique dynamique Jolt — barils qui roulent, caisses qui tombent, flottaison à la surface de l'eau.

## État actuel (2026-05-03)
- **Phase 1 — DONE** (commit `6e39298357`) : Dear ImGui v1.92.7 vendoré dans `extern/imgui/`, cible CMake `imgui` linkée à `openmw-lib` (`apps/openmw/CMakeLists.txt:206-208`), scaffold `MWGui::ImGuiOverlay` créé à `apps/openmw/mwgui/imguioverlay.{hpp,cpp}`.
- **Phase 2 — DONE** (commits `25ae22272f`, `c15ca00d78`) : overlay instancié dans `Engine`, F1 capté dans `inputmanagerimp` avec event interception.
- **Phase 2.5 — DONE** (à committer) : crash macOS résolu en basculant sur `imgui_impl_opengl2`, F1 réactivé. Détails plus bas.
- **Phase 3 — DONE** (à committer) : `MWGui::EntityInspector` à `apps/openmw/mwgui/entityinspector.{hpp,cpp}`, liste filtrée par type/distance/RefId, sélection par clic ou par "Pick from world" (raycast caméra).
- **Phase 4 — DONE** (à committer) : éditeur position/rotation/scale en DragFloat, plumb vers `World::moveObject`/`rotateObject`/`scaleObject` avec ré-validation de la sélection chaque frame. Comblé `JoltPhysicsSystem::updateScale` (était un no-op explicite) en wrappant le shape Jolt dans un `JPH::ScaledShape`.
- **Phase 5 — DONE** (à committer) : `MWGui::ObjectSpawner` à `apps/openmw/mwgui/objectspawner.{hpp,cpp}`, fenêtre séparée avec TabBar Static/Activator/Container/Light/Misc/Door, search filter, 3 modes de placement (player/crosshair/coords), reproduit le pattern `OpPlaceItem` console.
- **Phase 6 — TODO** : objets dynamiques Jolt (barils qui roulent, flottaison). Voir plus bas.
- **Phase 7 — TODO** : polissage (presets, undo/redo, onglet physics debug).

## Phase 2.5 — crash macOS résolu via switch OpenGL2
Cause racine : `engine.cpp:532-538` ne demande aucun `SDL_GL_CONTEXT_PROFILE_MASK`. Sur macOS le défaut SDL2 est GL 2.1 compat ; le loader `imgui_impl_opengl3` charge alors des symboles VAO/shader 3.x absents (silencieusement no-op) et le premier `glDrawElements` deref nullptr.

Fix appliqué (au lieu d'imposer un profil core qui casserait OSG) :
- `extern/CMakeLists.txt` : remplacer `imgui_impl_opengl3.cpp` par `imgui_impl_opengl2.cpp`.
- `apps/openmw/mwgui/imguioverlay.cpp` : swap include + appels `ImGui_ImplOpenGL3_*` → `ImGui_ImplOpenGL2_*` ; isolation explicite de l'état GL OSG dans `ImGuiDrawable::drawImplementation` via `state->disableAllVertexArrays()`, `setActiveTextureUnit(0)`, `setClientActiveTextureUnit(0)`, `unbindVertexBufferObject/ElementBufferObject` avant le draw, et `dirtyAllVertexArrays/Attributes` après. Sans cette isolation, OSG laisse `glActiveTexture` sur une unit > 0 et la font texture est bind sur la mauvaise unit → glyphes en carrés.
- `apps/openmw/engine.cpp` : restaurer la construction `mImGuiOverlay` + interceptor dans `prepareEngine`.

Compromis : on perd les features ImGui qui requièrent VAO/shaders (multi-viewport notamment). Non requises pour un overlay debug.

## Phase 3 — entity list + sélection (DONE)
Fichier `apps/openmw/mwgui/entityinspector.{hpp,cpp}`.

- **Itération** : `getWorldScene()->getActiveCells()` puis `cell->forEach` (collecte dans un vecteur, jamais muter pendant l'itération).
- **Filtres** : type (Static/Activator/Door/NPC/Creature/Container/Light/Misc/Other), distance max, substring sur RefId.
- **Sélection liste** : clic sur une `Selectable` row.
- **Sélection monde ("Pick from world")** : bouton qui passe l'inspector en mode pick. Clic gauche **hors widgets ImGui** → `World::getRenderingManager()->castCameraToViewportRay(nX, nY, 5000.0f, ignorePlayer=true)`. Esc cancel.
- **Robustesse** : la sélection (`MWWorld::Ptr`) est re-validée chaque frame contre la snapshot fraîche d'active-cells — pas de Ptr danglante si une cell est unloaded.

## Phase 4 — éditeur de l'entité sélectionnée (DONE)
- **Position** : `ImGui::DragFloat3` → `world->moveObject(ptr, osg::Vec3f, true, false)`. La sélection est mise à jour avec le `Ptr` retourné (peut changer de cell).
- **Rotation** : `DragFloat3` en degrés (clamp ±180°) → conversion deg→rad → `world->rotateObject(ptr, rotRad, RotationFlag_inverseOrder)`.
- **Scale** : `DragFloat` (range 0.1–10) → `world->scaleObject(ptr, scale)`.
- **Read-only** en haut : RefId, Type, Cell, Name, Count.
- **Live-update navigator** : `moveObject`/`rotateObject`/`scaleObject` appellent déjà `updateNavigatorObject` côté Bullet.

### Patch backend Jolt (était un trou)
`JoltPhysicsSystem::updateScale` était volontairement vide ("phase-10 task"). Implémenté en wrappant le shape Jolt courant dans un `JPH::ScaledShape` :
- `BodyInterface::GetShape(bodyId)` → si déjà `EShapeSubType::Scaled`, peel l'inner shape avant de re-wrap (pas d'accumulation de scale).
- `BodyInterface::SetShape(bodyId, scaled, /*updateMassProperties*/ false, EActivation::DontActivate)`.
- Marque `mObjectEntries[ref].mChanged = true` pour le navigator.
- Limitation : seuls les `mObjectBodies` (statiques) sont scalés. Les actors (`mActors` avec `JoltActor`) ne sont pas scalés — non standard via l'inspector.

`updatePosition` et `updateRotation` étaient déjà câblés sur Jolt — rien à faire.

## Phase 5 — spawner d'objet (DONE)
Fichier `apps/openmw/mwgui/objectspawner.{hpp,cpp}`. Fenêtre flottante séparée :

- **TabBar** : Static / Activator / Container / Light / Misc / Door — chaque tab itère `getESMStore()->get<T>()`.
- **Search** substring sur RefId.
- **Liste sélectionnable** filtrée.
- **3 modes de placement** : At player (pos+orient joueur), At crosshair (raycast écran centre), At coords (DragFloat3 manuel).
- **Z rotation** + **Count** sliders.
- **Spawn** désactivé tant que pas de sélection ; status "spawned X" / "failed: ..." après.

**Pattern spawn** strictement identique à `OpPlaceItem` console (`mwscript/transformationextensions.cpp:582-588`) :
```cpp
MWWorld::ManualRef ref(*ESMStore, refId, count);
ref.getPtr().mRef->mData.mPhysicsPostponed = !ref.getPtr().getClass().isActor();
ref.getPtr().getCellRef().setPosition(pos);
Ptr placed = world->placeObject(ref.getPtr(), cell, pos);
placed.getClass().adjustPosition(placed, true);
```

Cell resolution : si exterior, `positionToExteriorCellLocation` + `WorldModel::getExterior` ; sinon, cell du player.

## Phase 6 — objets dynamiques Jolt (TODO, ~3 jours)

Objectif : un onglet "Dynamic" dans le spawner qui crée un baril/caisse en `EMotionType::Dynamic`, qui tombe, roule, rebondit, et flotte sur l'eau. L'overlay devient un sandbox physique.

### État actuel
- `JoltPhysicsSystem` ne crée **aucun** body en `Dynamic` (juste `Static` pour objets/terrain et `Kinematic` pour projectiles + actors via CharacterVirtual).
- `World::isUnderwater(cell, pos)` et `CellStore::getWaterLevel()` existent → query gratuite.
- `Scene::updateObjectPosition`/`updateObjectRotation` poussent une transform vers OSG → sync rendu disponible.
- `RefData`/`CellRef` n'ont **pas** de field linear/angular velocity.

### Phase 6a — backend dynamic + spawner UI (~1.5 j)
1. Nouveau bucket `mDynamicBodies` dans `JoltPhysicsSystem` (parallèle à `mObjectBodies`).
2. `addDynamicBody(ptr, shape, mass)` créant un body `EMotionType::Dynamic`, layer `MOVING`. **Piège shape** : Jolt impose des shapes convexes pour Dynamic. Pour le MVP, shapes primitifs hardcodés (cylinder pour barils, box pour caisses, sphere pour pommes). Plus tard : `ConvexHullShape` depuis le mesh NIF (Jolt a la décomposition convexe baked-in).
3. Layer matrix : activer `MOVING vs NON_MOVING` + `MOVING vs MOVING`.
4. Dans `stepSimulation` post-step : itérer les dynamic bodies, lire `BodyInterface::GetPositionAndRotation`, pousser dans `RefData::setPosition` + `Scene::updateObjectPosition`/`updateObjectRotation`.
5. Côté UI : nouvel onglet "Dynamic" dans `ObjectSpawner` (ou checkbox "Spawn as dynamic"). Pour le MVP, présélection d'une poignée de RefIds connus (barrels, crates) avec leur shape primitif associé.
6. **Critère de réussite** : spawn un baril en hauteur, il tombe, roule sur le sol, rebondit légèrement.

### Phase 6b — buoyancy (~0.5-1 j)
Pas de helper Jolt natif pour la flottaison sur mesh.
1. À chaque tick, pour chaque dynamic body, échantillonner 4-8 points du AABB.
2. Compter combien sont sous `cell->getWaterLevel()`.
3. Force d'Archimède = `densité_eau × volume_submergé × g` vers le haut, appliquée au centre du volume submergé via `BodyInterface::AddForce`.
4. Drag linéaire/angulaire proportionnel à la vitesse pour amortir.
5. **Critère de réussite** : pousser un baril dans la mer à Seyda Neen, il flotte et oscille.

### Phase 6d — dynamic bodies dans le navmesh Recast/Detour (~1.5 j)

**État actuel** : un baril spawn dynamique tombe et roule, mais le navmesh ne le voit pas. Les NPCs lui marchent dedans (Jolt les bloque physiquement, mais ils essayent quand même → trajectoires mochent, IA confuse). Pour que le navmesh enregistre le baril comme obstacle, il faut le déclarer à `DetourNavigator::Navigator`.

**Problème de perf** : le navmesh utilise des tiles ; chaque `updateObject` invalide les tiles touchées et déclenche un rebuild dans le worker thread. Faire ça à 60 Hz pour chaque dynamic body écraserait le frame budget. Il faut une stratégie d'update.

**Stratégie retenue : sleep-driven + throttle.** Jolt a un système de sleep natif — un body s'endort quand sa vitesse est sous un seuil pendant ~0.5s. C'est exactement le moment où il a une position stable utile au pathfinding.

#### Implémentation

1. **Hook `JPH::BodyActivationListener`**
   - Une instance attachée à `JPH::PhysicsSystem` via `SetBodyActivationListener`.
   - `OnBodyActivated(BodyID)` : si le body est dans `mDynamicBodies`, le retirer du navmesh (`mNavigator->removeObject(id, guard)`). Le baril en mouvement n'est plus un obstacle valide.
   - `OnBodyDeactivated(BodyID)` : si le body est dans `mDynamicBodies`, le réinscrire au navmesh à sa position settled.

2. **Shape pour le navmesh**
   - Recast veut un `BulletShape` (cf. `updateNavigatorObject` dans `worldimp.cpp:1399`). On a deux cas :
     - **Mesh** dynamique : le `BulletShapeInstance` source est déjà capturé (cf. `meshSource` dans `promoteToDynamic`), on le réutilise tel quel.
     - **Primitives** : créer un `btBoxShape` synthétique aux dimensions `halfExtents` au moment du sleep, l'envelopper dans un `Resource::BulletShape` minimaliste.
   - Stocker ce shape sur la `DynamicBody` struct pour ne pas le reconstruire à chaque sleep/wake.

3. **Throttle anti-thrashing**
   - Un baril tapé répétitivement par un NPC = wake/sleep cycle court. Si on remove + add à chaque cycle, on lessivera les tiles.
   - Garder un timestamp du dernier `OnBodyDeactivated`. Si `OnBodyActivated` arrive < 1.0s plus tard, supprimer un timer pending au lieu de removeObject (le navmesh n'a pas encore vu le déactivation).
   - Symétrique : ne pas réinscrire avant un délai post-sleep (1s) ; garder un timer "à réinscrire à T+1s" dans une queue.

4. **Lifecycle au remove**
   - Si un dynamic body est remove (cell unload, F1 entity inspector delete, etc.), s'assurer qu'il est aussi remove du navmesh — sinon `ObjectId` orphan dans Recast.

5. **Cell-edge case**
   - Un baril qui roule traverse une frontière de tile : les deux tiles voisines se rebuilds. Acceptable car ça arrive déjà pour les doors et c'est rare en pratique.

#### Critère de réussite
- Spawn 5 barils dans un couloir étroit, attendre qu'ils s'arrêtent.
- NPC patrouille → trajectoire les contourne (visible dans le navmesh debug `togglepathgrid`).
- Pousser un baril : NPC ne le voit plus comme obstacle pendant qu'il roule, le re-considère après stabilisation.

#### Risques
- **Lock contention** : `mNavigator` est multi-thread. Le BodyActivationListener est appelé depuis le thread Jolt, mais `addObject`/`removeObject` doivent passer par le main thread (ou utiliser le guard). À vérifier — sinon enqueuer les events et les drainer en `stepSimulation`.
- **Shape miss pour primitives** : un `Resource::BulletShape` minimaliste fabriqué à la volée n'a pas tous les champs (mCollisionBox, mIsAnimated...). Tester que le navigator les tolère.
- **Throttle trop agressif** : si un NPC s'enferme entre 3 barils tous instables, les barils ne sont jamais ajoutés au navmesh. Acceptable — l'NPC les bumpera physiquement.

#### Effort
- Listener + plumbing : 0.5j
- Shape synthèse pour primitives : 0.5j
- Throttle + tests in-game : 0.5j

### Phase 6c — persistance dynamique dans les saves (DONE)
Sans rien faire, un baril spawné dynamic via le spawner revient static après save+reload. Persistance ajoutée :

1. Nouveau record ESM `ESM::DynamicBodyState` (`components/esm3/dynamicbodystate.hpp`) — shape (uint8), halfExtents (3×float), mass (float), rotation quat (4×float).
2. `ObjectState` gagne `mHasDynamicBody` + `mDynamicBody`. Ajoute / lit le sub-chunk optionnel `DYNB` via `getOptionalComposite` / `writeNamedComposite` — invisible aux saves antérieurs (back-compat naturelle).
3. `RefData` gagne `mDynamic : 1` + `mDynamicBody`, `setDynamic()` / `clearDynamic()`. `RefData::write` les pousse dans l'`ObjectState`, le constructeur `RefData(ObjectState)` les lit.
4. `IPhysicsBackend::promoteToDynamic` accepte un `const osg::Quat* initialRotation` optionnel — utilisé par le path cell-load pour rejouer la rotation settled sans passer par Euler (gimbal-lock évité).
5. `Scene::addObject` (cellpath des refs au load) appelle `physics.promoteToDynamic` après `Class::insertObject` si `RefData::isDynamic()`. Le static body créé par `insertObject` est immédiatement teardown par `promoteToDynamic`.
6. `JoltPhysicsSystem::stepSimulation` shadow l'`osg::Quat` live dans `RefData::mDynamicBody.mRotation` à chaque tick — la position va déjà dans `mPosition`. Au save, ces deux champs reflètent l'état settled.
7. `ObjectSpawner::doSpawn` appelle `placed.getRefData().setDynamic(dbs)` après `promoteToDynamic`, ce qui flippe `mChanged=true` (= ref écrite dans le save).

**Limite acceptée** : la velocity n'est pas persistée. Un baril qui roulait au moment du save respawn immobile et retombe par gravité. Acceptable — le comportement final est visuellement identique après quelques ticks. Pour persister la velocity : ajouter `mLinearVelocity` / `mAngularVelocity` à `DynamicBodyState` + sync à chaque tick.

### Risques Phase 6
- **Stack stability** : Jolt s'en sort très bien sur 50-100 bodies en pile — pas un risque réel.
- **NPCs qui tapent les barils** : déjà géré gratuitement par Jolt (CharacterVirtual collide avec MOVING).
- **Décomposition convexe pour mesh complexes** : différée à plus tard, MVP avec shapes primitifs.
- **Save game** : option (a) ci-dessus accepte la perte de vitesse au save. Option (b) = side-table custom record si on veut conserver l'élan.

## Phase 7 — polissage (optionnel, ~0.5 j)
- Sauvegarde/chargement de presets (positions/scales d'objets ajustés).
- Undo/redo basique.
- Onglet "physics debug" : nombre de bodies Jolt, FPS du physics step, état du joueur (`OnGround`, vélocité). À vérifier que `JoltPhysicsSystem` expose ces compteurs — sinon, ajouter des accesseurs sous flag `OPENMW_JOLT_TRACE`.

## Phase 8 — éditeur material live-tweaking + YAML registry (~3 j)

Fusion de l'ancien Phase 8 (per-instance live preview) avec `docs/material-override-plan.md` (YAML registry asset-side, hot-reload, matching mesh/refid/terrain). Le pane ImGui devient l'**UI d'authoring** au-dessus du registry ; le YAML est la **persistance**. Voir `material-override-plan.md` pour le détail des hooks (ShaderVisitor, terrain createPasses, MaterialRegistry singleton).

### Pourquoi merger
- Phase 8 originel résolvait *preview live* mais perdait tout au reload de cell (StateSet cloné). Le YAML registry résout *persistance + hot-reload* mais demande à l'utilisateur d'éditer un texte. Combiner les deux = workflow naturel : tweaker dans le pane, "Save" exporte vers `data/materials/refid_X.yaml`, le registry hot-reload immédiat applique partout.
- Une seule API `MaterialRegistry::matchMesh / matchTerrain` pilote le pane (lecture des overrides actifs sur la sélection) et le rendu (application du shader/uniforms).
- Évite de maintenir deux pipelines d'override : le pane écrit dans le registry, pas directement dans le StateSet OSG.

### État actuel
- Settings globaux `[Shaders]` (`auto use object normal maps`, `parallax mapping`, etc.) — pas de tweaking per-objet ni per-material.
- `Shader::ShaderVisitor::createProgram` (`components/shader/shadervisitor.cpp:631`) résout déjà `(templateName, defineMap)` → `Program`. Le matériel YAML s'insère ici (cf. material-override-plan.md ligne 93-108).
- `Resource::SceneManager` partage les StateSets entre instances (`SHARE_DUPLICATE_STATE`, `scenemanager.cpp:1003`). Pour un override par-RefId il faut un copy-on-write au moment de l'insertion (Phase 2 du material plan, ligne 153-163).
- `osg::UserValue("refId", ...)` n'est pas posé sur les nodes aujourd'hui ; à ajouter dans `mwrender/objects.cpp:60` pour permettre le matching par RefId.

### Phase 8a — registry YAML + matching mesh par diffuse/path (~6 h)
Implémente Phase 1 de `material-override-plan.md` :
- `components/material/{materialdef,materialregistry,materialapplier}.{hpp,cpp}` neufs.
- Linker `yaml-cpp` (déjà dispo dans le tree).
- `Resource::SceneManager` owner du registry, parser tout `data/materials/**/*.yaml` au boot.
- Hook `ShaderVisitor::createProgram` : si `MaterialRegistry::matchMesh(meshPath, nodeName, diffuseFilename)` retourne un hit, override `shaderPrefix` + merge `defineMap`, push uniforms/textures/state.

**Critère** : un YAML qui matche `tx_lantern` rend les lanternes avec un fragment shader custom ET un uniform animé. Aucune régression sur les autres meshes.

### Phase 8b — pane ImGui "Material" (~1 j)
Once 8a ships, l'inspector ImGui gagne un pane Material :
- Liste les matériels actuellement matchés sur la sélection (lus depuis `MaterialRegistry`). Si zéro match, propose "Create override for this mesh/refid" (templated YAML).
- Pour chaque uniform typé du matériel actif, génère le widget ImGui adapté (`DragFloat`, `ColorEdit3`, `Checkbox`).
- Bouton "Apply (live)" : pousse les valeurs courantes dans le `MaterialDef` runtime (sans rewriter le YAML) — le hot-reload du shader manager re-trigger une compile + re-apply à toutes les instances qui matchent. Latence ≤ 500 ms.
- Bouton "Save to YAML" : sérialise le `MaterialDef` modifié vers `data/materials/<name>.yaml` (atomic write : tmp + rename).
- Toggle "Per-instance only" : applique l'override uniquement à la sélection (forcer copy-on-write du StateSet du Ptr courant, sans toucher au registry global). Utile pour A/B tester avant d'écrire dans le YAML.

### Phase 8c — RefId matching + hot-reload (~1 j)
Implémente Phase 2 de `material-override-plan.md` :
- Plumber `setUserValue("refId", ...)` dans `mwrender/objects.cpp:60`.
- `MaterialRegistry` watch les mtimes des YAML, re-parse à chaque modif, flush le cache de match, trigger un `ShaderManager::triggerShaderReload()`.
- Pane ImGui : pendant l'édition d'un YAML externe (Vim/IDE), le pane se rafraîchit automatiquement à chaque save.

### Phase 8d (optionnel) — terrain override (~5 h)
Phase 3 de `material-override-plan.md`. Pas piloté par l'ImGui (sélection terrain pas exposée dans l'inspector aujourd'hui — ce serait une Phase 9 séparée).

### Pièges / risques
- **Conflit `SHARE_DUPLICATE_STATE`** : le pane Phase 8b option "Per-instance only" doit forcer un `setStateSet(new StateSet(*old, SHALLOW_COPY))` avant édition, comme `getWritableStateSet` (`shadervisitor.cpp:277`).
- **Si l'objet n'a pas de heightmap** (`_p.dds` absente), `parallaxScale` n'a aucun effet visible — le pane doit afficher "no heightmap" pour ce uniform.
- **Multi-material mesh** : un `BaseNode` peut avoir plusieurs Geodes (sub-materials différents). Le pane liste chaque sub-material séparément, chacun avec ses propres uniforms.
- **Threading** : touche l'OSG state graph. Les modifs passent par un `osg::NodeCallback` ou le main thread avant le draw — déjà géré côté `ShaderManager::triggerShaderReload` qui stoppe le viewer threading le temps du swap.
- **YAML invalide** : log + skip (pas de crash). Le pane affiche un statut d'erreur en bas.
- **Précédence multi-match** : `priority` desc, tie-break par filename alphabétique. Logger en `Debug::Warning` au load si deux règles à priorité égale matchent le même target.

### Critère de réussite global Phase 8
- Sélectionner un mur en pierre dans Balmora, ouvrir le pane Material, dragger `parallaxScale` de 0 à 0.1 → relief visible immédiat.
- Cliquer "Save to YAML" → fichier `data/materials/balmora_walls.yaml` créé. Reload de la cell → l'override persiste.
- Modifier le YAML dans un éditeur externe → hot-reload pousse la nouvelle valeur dans la session live sans restart.
- Toggle "Per-instance only" sur une seule lanterne dans une rangée de 5 → seule celle-là change ; les 4 autres restent vanilla.
- Toujours à faire (carry-over de l'ancien plan) :
  - Améliorer le DPI scale sur Retina (la fenêtre paraît petite malgré `style.FontScaleMain = dpiScale` + `ScaleAllSizes(dpiScale)` — peut-être bumper à `dpiScale * 1.5f`).
  - Highlight 3D bbox wireframe sur la sélection (skip volontairement de la phase 3, demande un `osg::Geode` attaché au scene graph).

## Risques connus / résolus
- **Conflit clavier avec MyGUI** : géré via `WantCaptureKeyboard`.
- **GL context macOS** : résolu Phase 2.5.
- **Curseur invisible avec overlay** : résolu via `io.MouseDrawCursor = true` (ImGui dessine son propre curseur software, indépendant du SDL_ShowCursor qui ne marche pas depuis le draw thread sur macOS).
- **Caméra bloquée avec overlay ouvert** : pattern d'éditeur — maintenir clic droit hors widgets pour reprendre temporairement le mouselook (relative mode), relâcher pour revenir au cursor ImGui.
- **Save game** : objets spawnés via `placeObject` persistent normalement (runtime copie dans le cell). Pour les éditions phase 4 idem. Le couplage save+dynamic body est traité en Phase 6c.

## Estimation restante
- Phase 6d (navmesh dynamic) : ~1.5 j
- Phase 7 (polissage) : ~0.5 j si désirée
- Phase 8a (registry YAML mesh match) : ~6 h
- Phase 8b (pane ImGui Material) : ~1 j
- Phase 8c (RefId match + hot-reload) : ~1 j
- Phase 8d (terrain override) : ~5 h optionnel

**Total minimum** (6d + 7 + 8a-c) : ~5 j. **Total complet** : ~6 j avec terrain.
