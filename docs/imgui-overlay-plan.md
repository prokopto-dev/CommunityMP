# Plan — menu ImGui « entity inspector » en overlay OpenMW

## Objectif fonctionnel
Toggle (F1 par défaut) → fenêtre flottante au-dessus du jeu permettant :
1. Lister / sélectionner les entités proches.
2. Éditer position, rotation, scale, et propriétés simples (nom, statique vs activateur, etc.).
3. Spawner un nouveau ref (mesh static, activator, container) à la position du joueur ou sous le curseur.

## Phase 1 — intégration tech (1 commit, ~2-3 h)
- **Dépendance** : ajouter `imgui` à `vcpkg.json` (déjà packagé). Linker `imgui` + `imgui-sdl2-binding` + `imgui-opengl3-binding`. Vendor en sous-arbre si vcpkg ne fournit pas le binding souhaité.
- **Module** : créer `apps/openmw/mwgui/imguioverlay.{hpp,cpp}` (un singleton léger, attaché à `WindowManager` ou à `Engine` au choix — préférer `Engine` pour rester hors MyGUI).
- **Boot** :
  - Init ImGui context + SDL2 + OpenGL3 backend après que la fenêtre SDL est créée (dans `OMW::Engine::go` après `mViewer` / `mWindow`).
  - Hook `processEvent` côté SDL avant que SDLUtil l'avale ; renvoyer "consommé" si ImGui veut clavier/souris.
- **Render** : nœud OSG `osg::Camera` post-render attaché à la caméra principale, avec un `Drawable` qui appelle `ImGui_ImplOpenGL3_NewFrame / RenderDrawData`. C'est le pattern utilisé par les ports OSG d'ImGui (~50 lignes).
- **Critère de réussite** : un `ImGui::ShowDemoWindow` visible en jeu, shutdown clean.

## Phase 2 — toggle + capture input (1 commit, ~1 h)
- **Hotkey** : F1 dans `keyboardmanager.cpp` ; ne pas passer par MyGUI pour éviter le couplage. Quand visible, l'overlay capture clavier/souris si `ImGui::GetIO().WantCaptureKeyboard / WantCaptureMouse`.
- **Pause** : optionnel, gel de `World::doPhysics` quand le menu est ouvert (toggle). Réutiliser le pause existant de l'inventaire.

## Phase 3 — entity list + sélection (1 commit, ~3-4 h)
- **Source** : itérer `MWBase::Environment::get().getWorldScene()->getActiveCells()` puis `cell->forEach(visitor)` pour collecter les `MWWorld::Ptr` actives.
- **Filtres UI** : par classe (Static / Activator / Door / NPC / Creature / Container / Light / Misc), par distance au joueur, par RefId substring.
- **Sélection au clic dans le monde** : raycast depuis la caméra via `mPhysics->castRay` ; le `mHitObject` Ptr alimente la sélection.
- **Highlight** : surcouche OSG (bbox wireframe rouge) sur le `BaseNode` du Ptr sélectionné — OpenMW a déjà un système debug-draw réutilisable.

## Phase 4 — éditeur de l'entité sélectionnée (1 commit, ~4-5 h)
- **Position** : `ImGui::DragFloat3` ; appliquer via `world->moveObject(ptr, x, y, z, false)`.
- **Rotation** : 3 sliders en degrés ; `world->rotateObject(ptr, vec, MWBase::RotationFlag_inverse)`.
- **Scale** : `world->scaleObject(ptr, scale)`.
- **Refs simples** : RefId / refnum / cell name (read-only, sauf "duplicate to other cell" pour plus tard).
- **Live-update navigator** : `world->updateNavigatorObject` après chaque mutation pour que le navmesh suive (déjà fait par moveObject ; vérifier rotateObject/scaleObject).

## Phase 5 — spawner d'objet (1 commit, ~3 h)
- **Catalogue** : combobox alimentée par `MWBase::Environment::get().getESMStore()->get<ESM::Static>()` (et Activator, Container, Light selon onglet).
- **Placement** : "at player" (position + orientation joueur), "at crosshair" (raycast forward 500 unités), "at coords" (input).
- **Création** : `MWBase::Environment::get().getWorld()->placeObject(refid, cellRef, position, rotation, count)` — l'API existe déjà pour les commandes console `PlaceAtMe`. Réutiliser strictement.

## Phase 6 — polissage (optionnel, demi-journée)
- Sauvegarde/chargement de presets (positions/scales d'objets ajustés).
- Undo/redo basique.
- Onglet "physics debug" : afficher dans ImGui le nombre de bodies Jolt, FPS du physics step, état du joueur (`OnGround`, vélocité).

## Risques
- **Conflit clavier avec MyGUI** : la console (`~`) et l'inventaire grabent déjà le clavier ; il faut décider qui a priorité quand les deux veulent capturer. Solution : si MyGUI a une fenêtre modale ouverte, l'overlay reste visible mais ne capture pas.
- **GL context** : si OpenMW utilise un contexte OSG core-profile sans compat extensions, le binding `opengl3` doit cibler 3.3 core. À tester sur macOS (Apple drops OpenGL > 4.1).
- **Save game** : les objets spawnés via `placeObject` sont persistés (le runtime copie dans le cell). Les scale/rotation édités le sont aussi via les chemins MoveObject. Donc pas de risque de "perdre" les changements au save, mais ça pollue les saves — peut-être ajouter un toggle "session only" qui fait un snapshot et restore au prochain load.

## Estimation totale
~14-18 h de dev sérieux, étalé en 5 commits indépendants. Phase 1 est de loin la plus risquée (binding OSG/SDL2/OpenGL3) ; les autres sont du UI + glue par-dessus une API existante.
