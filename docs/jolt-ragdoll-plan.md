# Plan — Ragdoll des cadavres sous Jolt

## Contexte

Vanilla MW joue une animation `death1`-`death5` à la mort. Le corps est figé dans la pose finale jusqu'à disparition (loot, fade-out, despawn). On veut remplacer la pose statique par une simulation physique courte: bras qui tombent, corps qui s'affaisse selon le terrain, position d'arrêt qui dépend de l'environnement.

Jolt fournit nativement les pièces (`RagdollSettings`, `Ragdoll`, `SkeletalAnimation` blend), mais l'OpenMW pipeline n'a aucun rigging d'animation côté physique pour l'instant — toutes les actor-animations sont jouées par OSG (`SceneUtil::Animation`) et les colliders Jolt utilisent une simple capsule autour de l'acteur, pas le squelette.

## Objectif

À la mort:
1. Capturer la pose courante de chaque os depuis le rig OSG.
2. Construire un `JPH::Ragdoll` à partir du squelette NIF (1 body rigide par os principal, joints contraints).
3. Échanger le contrôle: la `CharacterVirtual` est désactivée, le ragdoll prend le relais.
4. À chaque step physique, lire la pose des bodies du ragdoll et l'écrire dans la matrice transform de chaque os OSG → le mesh visuel suit la simulation.
5. Détection de "stabilité" (vitesse linéaire+angulaire toutes < seuil pendant 2 s) → endormir les bodies, garder la dernière pose.

## Phases

### Phase 1 — Audit & infrastructure (~3-4 h)

- Repérer le point de mort canonique: `MWMechanics::CreatureStats::isDead()` + transition d'état `Animation::play("death*")`. Hook l'événement.
- Ajouter `MWPhysics::JoltRagdoll` (nouvelle classe) à côté de `JoltActor`. Owner: `JoltPhysicsSystem` via une `std::unordered_map<ESM::RefNum, std::unique_ptr<JoltRagdoll>> mRagdolls`.
- Spike: créer un ragdoll simple à partir d'un squelette MW connu (ex. `meshes/b/b_n_imperial_m_skeleton.nif`) en hardcodant les os principaux (Bip01 Spine, Bip01 Pelvis, Bip01 L UpperArm, etc.) — pour valider que Jolt simule correctement avant d'industrialiser le rigging.
- Définir le mapping OS → forme physique: `Capsule` pour les membres (radius depuis l'épaisseur du mesh skinning, length depuis la distance os parent → enfant), `Box` pour le torse / pelvis, `Sphere` pour la tête.

### Phase 2 — Rigging à partir du NIF (~6-8 h)

- Lire le `NiSkinInstance` du mesh principal pour obtenir la liste des os qui ont une influence ≥ 5 % sur des vertices. C'est notre "set ragdoll" — 12 à 18 os typiquement.
- Calculer pour chaque os:
  - `parent` (depuis la hiérarchie NIF)
  - `bind pose transform`
  - `bounding capsule` à partir des vertices skinnés à cet os
- Construire `JPH::RagdollSettings` avec:
  - 1 `JPH::BodyCreationSettings` par os (motion type Dynamic, layer ACTOR_PROBE → on filtre déjà, OK)
  - 1 `JPH::SwingTwistConstraint` par paire parent-enfant, avec angles humains réalistes (épaule: swing 90° / twist 60°, coude: hinge 0°-150°, etc.). Table de limites par nom d'os.
  - Mass distribution réaliste: tête ~5 kg, torse ~30 kg, bras ~5 kg, jambes ~12 kg.
- Cache le `RagdollSettings` par RefId-de-mesh (pas par instance) pour éviter de re-rigger à chaque mort de garde imperial.

### Phase 3 — Bascule à la mort (~2-3 h)

Sur l'événement de mort:
1. Désactiver la `CharacterVirtual` (`actor->setActive(false)` existe déjà — vérifier qu'il est bien shared entre frames).
2. Capturer la pose actuelle: pour chaque os du set ragdoll, lire sa world-space transform OSG.
3. `ragdoll = settings.CreateRagdoll(collisionGroup, userData, mPhysicsSystem)`. Initialiser chaque body avec la pose capturée + une vélocité linéaire/angulaire qui correspond au mouvement courant de l'acteur (sinon le ragdoll "saute" depuis une pose figée).
4. `ragdoll->AddToPhysicsSystem(JPH::EActivation::Activate)`.
5. Stocker dans `mRagdolls`.
6. Instrument: `OPENMW_JOLT_TRACE=1` log "ragdoll spawned for X with N bodies".

### Phase 4 — Sync de pose ragdoll → mesh (~3 h)

À chaque `stepSimulation`:
- Pour chaque ragdoll actif, lire `body->GetWorldTransform()` pour chaque os.
- Convertir en transform local (relatif au parent) et l'écrire dans le `osg::MatrixTransform` correspondant.
- C'est le miroir inverse de ce que fait `Animation::handleAnimationFrame` — il faudra probablement bypasser `Animation::update` quand un ragdoll est actif (poser un flag `mRagdollDriven = true` sur l'animation, qui short-circuit les calculs d'animation traditionnels).

### Phase 5 — Stabilité, sleep, cleanup (~2 h)

- Détecter la stabilité: par body, accumuler les frames où `|GetLinearVelocity()| < 0.5 m/s` et `|GetAngularVelocity()| < 0.1 rad/s`. Au-delà de 2 s, désactiver le body (`SetMotionType(Static)`).
- Quand TOUS les bodies du ragdoll sont stables → enlever le ragdoll de la simulation, geler la pose finale dans le mesh OSG (transforms écrits une dernière fois).
- À la disparition du cadavre (loot complet, despawn, cell unload) → `ragdoll->RemoveFromPhysicsSystem()` + destruction.
- Sauvegarde: persister la pose finale dans le savegame (15 transforms × 12 bytes = 180 bytes par cadavre, négligeable).

### Phase 6 — Polish (~2-4 h, optionnel)

- Hits sur cadavres: si on tape un corps inerte, ré-activer son ragdoll avec une impulsion à la position du hit.
- Eau: si le cadavre tombe dans l'eau (water sensor contact), activer un mode flottaison (force ascensionnelle proportionnelle au volume submergé).
- Limit-break visuel: si une force d'impact > seuil sur un joint (explosion proche), désactiver le constraint pour quelques secondes pour un effet "démantèlement".

## Risques / questions ouvertes

| Risque | Détail | Mitigation |
|---|---|---|
| Coût mémoire | Chaque ragdoll = ~15 bodies × ~500 octets = 7.5 KB. 50 cadavres dans un dungeon = 375 KB. OK mais à surveiller. | Cap dur sur `mRagdolls.size()`, recycler le plus vieux. |
| Coût CPU | Jolt simule O(n_bodies + n_constraints). Stabilisation rapide attendue (< 2 s), mais 50 ragdolls actifs = 750 bodies dans la broadphase. | Sleep agressif (phase 5), bodies passés à Static une fois stables. |
| Interactions ragdoll ↔ ragdoll | Deux cadavres qui se chevauchent → résolution de pénétration coûteuse, peut osciller. | Layer dédié `RAGDOLL` qui ne collisionne qu'avec NON_MOVING (pas entre cadavres). |
| Anims spéciales | "Lich" qui meurt en s'effondrant en fumée → pas un ragdoll. Persistent NPC qui revit (Bethesda's writing) → idem. | Liste d'opt-out par RefId / classe d'acteur. |
| Sauvegarde compat | Anciens saves n'ont pas de pose ragdoll persistée. | Au load, si pose absente → utilise la pose de death-anim figée comme avant. |
| Os manquants dans le NIF | Certains modèles MW ont des squelettes simplifiés (tail, wing). | Fallback: tout os non-mappé reste skinned-driven, pas physique. |

## Estimation totale

Phases 1-5: ~16-20 h de dev focus.
Phase 6 (polish): +6 h pour l'expérience "wow".

## Ordre d'attaque recommandé

1. Phase 1 spike — un ragdoll de test simulé seul, pas branché au gameplay. Vérifier que Jolt produit une chute crédible.
2. Phase 2 + 3 — rigging automatique + bascule, sur un seul type de creature pour limiter le scope (humain).
3. Phase 4 — sync pose. À ce point on a un humain qui meurt et tombe correctement.
4. Phase 5 — sleep & cleanup, indispensable avant scaling.
5. Élargir aux autres squelettes (Khajiit, bête, golem) — ré-utilise l'infra de phase 2.
6. Phase 6 selon temps.
