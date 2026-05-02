# Plan complet — corriger l'intégration CharacterVirtual sous Jolt

## Contexte

Symptômes encore observés après les fixes courants (commits `4d22dc965d` gravité, `c8a598de8b` Z-up settings, `da1703b85f` accumulation gravité, `eb825b6c62` collisions statiques, `d484efd6c5` heightfield) :

- Gravité « weird » (probablement réduite après commits récents — à confirmer)
- Joueur ne suit pas la direction de la caméra
- Saut ne s'enregistre pas
- NPCs ne touchent pas le sol et ne bougent pas

Tous ces symptômes sont compatibles avec **une intégration CharacterVirtual incomplète** : Jolt simule peut-être correctement, mais l'aller-retour gameplay ↔ physique est cassé en plusieurs points.

## Audit du gap Bullet → Jolt

### Bullet (référence)
1. `World::queueMovement(ptr, vel)` → `mPhysics->queueObjectMovement` met `vel` dans `mActorsFrameData`.
2. `MovementSolver::move` itère **par actor par sub-step** :
   - Lit `actor.mInertia` (vélocité accumulée gravité, séparée de l'input).
   - Calcule `velocity = input * speedFactor` puis `displacement = (velocity + inertia) * dt`.
   - Sweep test Bullet, gestion slope/step-up/step-down.
   - Met à jour `actor.mInertia.z -= GravityConst * UnitsPerMeter * dt` si pas onGround.
   - Reset `actor.mInertia = 0` si onGround (et pas slope).
   - Set `actor.mIsOnGround` selon le résultat du sweep.
3. `World::moveActors` propage la nouvelle position vers `MWWorld::Ptr.refData`.
4. Gameplay lit `world->isOnGround(ptr)` qui forwarde à `actor->getOnGround()`.

### Jolt (état actuel)
1. `queueObjectMovement` met `vel` dans `mQueuedMovement`. ✓
2. `stepSimulation` :
   - Lit la queue, calcule `zVel` (input ou current ou 0 selon onGround), ajoute `gravity * dt`. **Conflate input et inertie.**
   - `cv->SetLinearVelocity(input.x, input.y, zVel)`.
   - `mJoltSystem->Update(dt)` (ne touche pas les CV).
   - `cv->ExtendedUpdate(dt, gravity, ...)` qui résout les collisions et update ground state.
3. `moveActors` → `world->moveObject(ptr, cv->GetPosition(), false, false)`. ✓
4. Gameplay lit `isOnGround(ptr)` → `actor->isOnGround()` → `mIsOnGround` cached depuis `cv->IsSupported()`. ✓ en théorie.

### Gaps identifiés

| Aspect | Bullet | Jolt actuel | Impact |
|---|---|---|---|
| **Gravité** | `mInertia.z -= g*dt` séparé de l'input | Mélangé dans la même boucle, branche sur `vel.z()!=0` | Saut peut être écrasé si la gameplay envoie un nouveau Z avant que l'apex ne soit atteint |
| **Inertie XY** | `mInertia.x/y` reste après landing (slide naturel) | Toujours écrasé par `vel.x/y` chaque frame | Pas de slide, mouvements anti-physiques |
| **Speed factor** | Appliqué dans MovementSolver à partir de `movementSettings.mSpeedFactor` | L'input arrive déjà multiplié, mais le scaling temporel peut être différent | Vitesse de marche/course peut-être incorrecte |
| **Stick-to-floor** | Implicite (sweep test descend) | `mStickToFloorStepDown` était (0,-0.5,0) Y-up — **fixé `c8a598de8b`** | Ground flickering avant fix |
| **Step-up** | `Stepper::step` avec `sStepSizeUp = 34` | `mWalkStairsStepUp` était (0,0.4,0) Y-up — **fixé `c8a598de8b`** | Bloqué sur petits obstacles avant fix |
| **getActor** | Retourne `Actor*` réel, lu par 30+ call sites | Retourne `nullptr` partout | Branches gameplay « if(actor)... » silencieusement skippées |
| **setRotation** | `Actor::setRotation` propage à btCollisionObject | JoltActor n'expose pas de setRotation, et ce n'est pas appelé | Player face = camera angle reste OK (lu via RefData) mais incohérent côté physique |
| **Inner body filtering** | N/A | `mInnerBodyShape = mShape` créé dans le world. Le filter ignore la sienne ; entre actors, peut causer overlap | NPCs se collisionnent peut-être entre eux par leur inner body |
| **Slope sliding** | Vanilla MW = max 46° (fSlopeBraking GMST) | `mMaxSlopeAngle = 45°` hardcodé | Différence subtile, OK pour l'instant |
| **Water walking** | `mCanWaterWalk` flag honoré dans MovementSolver | Pas implémenté | Sort water-walk cassé |
| **Swim** | Velocity ignore gravity, custom buoyancy | Pas implémenté | Nage cassée |
| **Inertia reset on land** | Explicite : si `isOnGround && !isOnSlope` → mInertia = 0 | onGround branch met Z=0 mais XY reste de l'input | Comportement de chute différent |
| **Stuck detection / unstuck** | `mStuckFrames`, `mLastStuckPosition`, déplacement anti-stuck | Pas implémenté | Player peut se coincer dans la géométrie |
| **getPtr déplacé** | `Actor::updatePosition` re-snap depuis RefData (pour téléports / scripts) | JoltActor n'a pas d'équivalent | Téléports script / `tcl` vers position cassent l'état physique |

## Plan en 5 phases

### Phase 1 — diagnostic instrumenté (~1 h, 1 commit, **revert avant merge**)

Ajouter un bloc de logging temporaire dans `JoltPhysicsSystem::stepSimulation`, conditionné sur `OPENMW_JOLT_TRACE=1` env var :

```cpp
if (sTraceEnabled && (frameNumber % 30 == 0))
{
    for (auto& [ref, actor] : mActors)
    {
        auto* cv = actor->getCharacter();
        const auto p = cv->GetPosition();
        const auto v = cv->GetLinearVelocity();
        const auto gs = cv->GetGroundState();
        Log(Debug::Info) << "[jolt-trace] " << actor->getPtr().getCellRef().getRefId()
            << " pos=(" << p.GetX() << "," << p.GetY() << "," << p.GetZ() << ")"
            << " vel=(" << v.GetX() << "," << v.GetY() << "," << v.GetZ() << ")"
            << " ground=" << static_cast<int>(gs);
    }
}
```

Lancer le jeu **2 minutes** avec `OPENMW_JOLT_TRACE=1`, observer :
- Le joueur voit-il sa Z baisser quand spawné en l'air ?
- `ground` passe-t-il à `OnGround=0` quand il devrait ?
- Les NPCs ont-ils une vélocité non nulle ?

**Critère de réussite** : on a une trace utilisable. Décide ensuite des phases suivantes selon les vrais symptômes vs supposés.

### Phase 2 — séparer input et inertie (~3 h, 1 commit)

Sur le pattern Bullet : ajouter `osg::Vec3f mInertia` à `JoltActor`. Réécriture de la boucle de step 1 :

```cpp
for (auto& [ref, actor] : mActors)
{
    auto* cv = actor->getCharacter();
    const osg::Vec3f input = drainQueuedMovement(ref);   // velocity intent
    osg::Vec3f& inertia = actor->mInertia;

    // Inertie verticale : gravité accumule en l'air, reset à terre
    if (actor->mIsOnGround && !actor->mIsOnSlope)
        inertia.z() = 0.f;
    else
        inertia.z() += stepGravity.GetZ() * dt;

    // Jump impulse : input.z() écrase l'inertie verticale (one-shot)
    if (input.z() > 0.f)
        inertia.z() = input.z();

    // Vélocité totale = input horizontal + inertie verticale
    const JPH::Vec3 totalVel(input.x(), input.y(), inertia.z());
    cv->SetLinearVelocity(totalVel);
}
```

Critère : saut donne un arc visible, gravité reproductible, statique = stable.

### Phase 3 — peupler `getActor` / `getProjectile` avec vrais wrappers (~6-8 h, 2-3 commits)

Refactor architectural. Choix entre :
- **(a) wrapper minimal** : modifier `Actor` pour que son constructeur accepte un `nullptr` scheduler + désactiver les chemins Bullet-only, instancier `Actor` depuis JoltPhysicsSystem.addActor en plus du `JoltActor`.
- **(b) interface** : créer `IPhysicsActor` exposant les ~20 méthodes utilisées par mwworld/mwmechanics (déjà inventoriées dans le commit `62afe3ad87`), faire que `Actor` (Bullet) et `JoltActor` l'implémentent, changer `IPhysicsBackend::getActor` pour retourner `IPhysicsActor*`.

Recommandation : **(b)**, plus propre, c'est ce que la phase 10 du plan d'origine prévoyait. Mais commence par les méthodes lues effectivement (`getCollisionMode`, `setActive`, `enableCollisionBody`, `enableCollisionMode`, `isWalkingOnWater`, `getCollisionShapeType`, `getHalfExtents`, `adjustPosition`).

Critère : `mPhysics->getActor(player)` retourne non-null sous Jolt, le code gameplay qui gate sur cette présence s'exécute, le navigator reçoit `addAgent` pour les NPCs.

### Phase 4 — gameplay state sync (~3 h, 1 commit)

Ajouter à `JoltActor` :
- `setRotation(osg::Quat)` qui tourne la `CharacterVirtual` (et son inner body).
- `updatePosition()` re-snap depuis `Ptr::refData` (pour téléports script).
- `setOnGround(bool)`, `getOnSlope()`, `setStandingOnPtr()` etc. pour matcher l'API `Actor`.
- Refresh `mIsOnSlope` dans `refreshState()` à partir de `cv->GetGroundState() == OnSteepGround`.

Vérifier que `World::rotateObject(player, ...)` propage à `JoltActor::setRotation` via le wrapper de phase 3.

Critère : `tcl` (no-clip), `coc` (téléport vers cellule), saut sur slope, fonctionnent comme en Bullet.

### Phase 5 — eau, slope, stuck detection (~4-6 h, 2 commits)

- **Water** : détecter si `cv` est sous `mWaterBody`, désactiver gravity, activer buoyancy upward.
- **Water-walk** : si `mCanWaterWalk` et le sample de surface hit water → set la position Z du sample au water level, set `IsSupported = true`.
- **Slope behaviour vanilla** : slide vers le bas si slope > maxSlope, anchorer au sol sinon.
- **Stuck detection** : si position n'a pas bougé > 3 frames consécutives malgré velocity > 0, appliquer un offset random ±5 unités.

Critère : nager dans la côte fonctionne, water-walk fonctionne, ne pas se coincer dans la géométrie complexe (caverne ash).

## Estimation totale
~17-20 h, 7-8 commits. **Phase 1 d'abord** — sans diagnostic on tire au pifomètre.

## Risques

- **Inner body double-collision entre actors** : si confirmé en phase 1, faut soit virer l'inner body (perdre les ray casts qui hit actors) soit le mettre dans un layer dédié `ACTOR_INNER` qui ne collide qu'avec NON_MOVING.
- **Refresh state lag** : `IsSupported()` peut retourner stale si `refreshState` est appelé avant que l'`Update` ait tourné. Vérifier l'ordre.
- **Phase 3 (interface refactor)** est invasive, va casser la compilation Bullet-only momentanément. Faire en deux passes : d'abord la nouvelle interface (les deux backends l'implémentent), puis migration des call sites.

## Ordre d'exécution suggéré

```
Phase 1 (diagnostic) → décision sur cause root
   ↓
Phase 2 (inertie) → débloque gravité/jump propres
   ↓
Phase 3 (getActor wrapper) → débloque navmesh + branches gameplay
   ↓
Phase 4 (state sync) → débloque tcl/coc/téléports
   ↓
Phase 5 (eau, stuck) → débloque exploration complète
```

Chaque phase est testable isolément et apporte un gain visible.
