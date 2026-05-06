# Main menu bar + per-pane visibility toggles

## Contexte

Aujourd'hui F1 montre / cache l'**ensemble** de l'overlay ImGui. Tant que c'est visible, **tous** les panes (`EntityInspector`, `ObjectSpawner`, `MaterialEditor`, `ShaderSettings`, `ScreenshotPane`) s'affichent en même temps via leurs `ImGui::Begin/End` indépendants. C'est l'approche "tout ou rien" — plus on ajoute de panes, plus l'écran se remplit, et l'utilisateur n'a pas de moyen rapide de masquer ce dont il n'a pas besoin sans fermer l'overlay entier.

**Objectif :**
1. Garder F1 comme toggle global.
2. Quand l'overlay est visible, afficher en haut une barre de menu ImGui (`BeginMainMenuBar`) qui sert de "table des matières" et permet de cocher / décocher chaque pane individuellement.
3. Le bouton "X" (close) de chaque pane miroite la même bool — fermer la fenêtre par la croix décoche aussi la case dans le menu.

Pas d'IO disque : pas de persistance des cases pour ce premier passage. Sessions courtes, défauts raisonnables suffisent.

## Approche

### 1. Drapeaux de visibilité par pane

Chaque pane reçoit un `bool mVisible` membre + un getter `bool& visibleFlag()` qui retourne une référence (le menu et le bouton X doivent partager le même `bool`).

Fichiers : `apps/openmw/mwgui/{entityinspector,objectspawner,materialeditor,shadersettings,screenshotpane}.{hpp,cpp}`.

**Pattern par pane :**
```cpp
// .hpp
bool& visibleFlag() { return mVisible; }
private: bool mVisible = ...;  // default per pane (table below)

// .cpp draw()
void XxxPane::draw() {
    if (!mVisible) return;
    ImGui::SetNextWindowSize(...);
    if (!ImGui::Begin("Xxx", &mVisible)) {  // le `&mVisible` pose la croix
        ImGui::End();
        return;
    }
    ...
}
```

**Défauts proposés** (visible = `true`) :
- `EntityInspector` — true
- `MaterialEditor` — false (pleine fenêtre, on l'ouvre à la demande)
- `ObjectSpawner` — false
- `ShaderSettings` — false
- `ScreenshotPane` — false

L'inspector reste prioritaire parce que c'est le point d'entrée pour tout le reste (sélection puis Material section). Les autres s'ouvrent via le menu.

### 2. Barre de menu principale

Nouvelle fonction libre dans `imguioverlay.cpp`, appelée juste après `ImGui::NewFrame()` et avant les `pane->draw()` :

```cpp
void drawMainMenuBar(ImGuiOverlay& overlay) {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("View")) {
        if (auto* p = overlay.entityInspector())   ImGui::MenuItem("Entity Inspector",  nullptr, &p->visibleFlag());
        if (auto* p = overlay.materialEditor())    ImGui::MenuItem("Material Editor",   nullptr, &p->visibleFlag());
        if (auto* p = overlay.objectSpawner())     ImGui::MenuItem("Object Spawner",    nullptr, &p->visibleFlag());
        if (auto* p = overlay.shaderSettings())    ImGui::MenuItem("Shader Settings",   nullptr, &p->visibleFlag());
        if (auto* p = overlay.screenshotPane())    ImGui::MenuItem("Screenshot",        nullptr, &p->visibleFlag());
        ImGui::Separator();
        if (ImGui::MenuItem("Hide all"))  { /* set every flag to false */ }
        if (ImGui::MenuItem("Show all"))  { /* set every flag to true  */ }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools")) {
        if (ImGui::MenuItem("Reload shaders")) {
            auto* sm = MWBase::Environment::get().getResourceSystem()->getSceneManager();
            if (sm) sm->getShaderManager().triggerShaderReload();
        }
        if (ImGui::MenuItem("Recreate shaders (full pass)")) {
            auto* sm = MWBase::Environment::get().getResourceSystem()->getSceneManager();
            if (auto* r = MWBase::Environment::get().getWorld()->getRenderingManager())
                if (auto* root = r->getObjects().getRootNode())
                    sm->recreateShaders(root);
        }
        if (ImGui::MenuItem("Reload materials from disk")) {
            auto* sm = MWBase::Environment::get().getResourceSystem()->getSceneManager();
            auto* reg = sm ? sm->getMaterialRegistry() : nullptr;
            if (reg) reg->reload(MWBase::Environment::get().getResourceSystem()->getVFS());
            if (sm) sm->getShaderManager().triggerShaderReload();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        ImGui::TextDisabled("F1            toggle overlay");
        ImGui::TextDisabled("Esc           cancel pick mode");
        ImGui::TextDisabled("Right-click   drag world camera through overlay");
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}
```

Câblage dans `ImGuiDrawable::drawImplementation` (`imguioverlay.cpp:74-86`) :
```cpp
ImGui::NewFrame();
drawMainMenuBar(*mOwner);
if (auto* inspector = mOwner->entityInspector())   inspector->draw();
if (auto* spawner   = mOwner->objectSpawner())     spawner->draw();
if (auto* materials = mOwner->materialEditor())    materials->draw();
if (auto* shaders   = mOwner->shaderSettings())    shaders->draw();
if (auto* shot      = mOwner->screenshotPane())    shot->draw();
ImGui::Render();
```

Le main menu bar prend ~24 px en haut de l'écran ; il ne capture pas la souris au-delà de sa propre hauteur, donc les panes ouvertes dessous restent interactives normalement.

### 3. Coexistence avec le skip-frames du screenshot

`ImGuiDrawable::drawImplementation` saute déjà tout le bloc ImGui quand `consumeSkipFrame()` retourne true (capture screenshot propre). Le menu bar tombe avec — c'est ce qu'on veut, sinon il apparaîtrait dans le screenshot. Pas de changement nécessaire ici.

### 4. Pas de persistance (pour l'instant)

ImGui peut persister l'état des fenêtres dans un `imgui.ini` mais c'est désactivé (`io.IniFilename = nullptr` à `imguioverlay.cpp:127`). On garde le défaut pour cette première passe — la table de défauts ci-dessus est suffisamment opinionée pour que l'utilisateur n'ait à toggler que ce qu'il veut.

Suivi possible si demandé : sérialiser une petite struct `{bool entity, bool materials, …}` dans `<userdata>/imgui-overlay.json` à la fermeture du jeu, recharger au boot. ~15 LOC dans `ImGuiOverlay`.

## Fichiers modifiés

- `apps/openmw/mwgui/entityinspector.{hpp,cpp}` — ajout `mVisible` + gate dans `draw()` + `Begin("…", &mVisible)`
- `apps/openmw/mwgui/objectspawner.{hpp,cpp}` — idem
- `apps/openmw/mwgui/materialeditor.{hpp,cpp}` — idem (sur la classe `MaterialEditor`, pas `drawMaterialDefInline`)
- `apps/openmw/mwgui/shadersettings.{hpp,cpp}` — idem
- `apps/openmw/mwgui/screenshotpane.{hpp,cpp}` — idem
- `apps/openmw/mwgui/imguioverlay.cpp` — nouvelle fonction `drawMainMenuBar`, appelée avant les panes ; pas de membres en plus (la barre ne porte aucun état propre).

## Vérification

1. **Build** : `ninja openmw` dans `build-jolt-test`, idem dans `build`.
2. **F1 toggle** : ouvrir l'overlay → la barre de menu apparaît en haut, l'Entity Inspector est ouvert, les autres fenêtres sont fermées.
3. **View → Material Editor** → la fenêtre apparaît ; cliquer le X de la fenêtre → la case se décoche.
4. **View → Hide all** → uniquement la barre de menu reste à l'écran.
5. **Tools → Reload shaders** → vérifier qu'un mat compilé se recharge (par ex. modifier `objects.frag` à la main, cliquer Reload, voir le pixel changer).
6. **Screenshot clean** : Screenshot pane visible → cocher "Hide overlay during capture" → Take screenshot → vérifier que le PNG ne contient ni les fenêtres ni la barre de menu.
7. **Esc** : pendant un Pick mode, Esc annule comme avant — le menu n'intercepte rien.

## Points ouverts (non bloquants)

- Si on veut une icône ou un titre custom dans la barre (ex. `OpenMW debug v0.51`), on peut ajouter un `ImGui::TextDisabled` aligné à droite via `GetWindowWidth() - GetCursorPosX()` — décoratif uniquement.
- Si on veut un raccourci clavier par pane (ex. `Ctrl+E` pour Entity Inspector), `MenuItem` accepte un troisième arg `shortcut` — c'est juste un label, le binding reste à câbler dans `processEvent`. Premier passage : pas de raccourcis.
