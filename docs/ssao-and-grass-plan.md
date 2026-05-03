# Plan — SSAO standalone + grass animée par vertex shader

## Contexte (audit du code existant)

### SSAO
Une SSAO horizon-based est **déjà câblée** dans `files/data/shaders/internal_raycast.omwfx` :
- uniforms `uSsao`, `uSsaoRadius`, `uSsaoStrength`, `uSsaoSamples` (lignes 34-55)
- fonction `ambientOcclusion()` (lignes 393-435), GTAO-style 1-tap par sample
- appliquée multiplicativement sur la couleur finale ligne 1043 : `col.rgb * shadow * ao`

Limites du code actuel :
- 1 anneau de samples (pas de hémisphère cosine-weighted), donc bruit visible aux faibles counts.
- Pas de blur bilatéral → bruit reste à l'écran (le seul anti-aliasing est le dither world-anchored).
- Mélangé dans un méga-shader (`internal_raycast.omwfx` regroupe contact shadows, SSAO, SSR, halo, god rays, atmosphere, vol fog, vol clouds, eye adapt, wetness). Difficile à débugger / iterer en isolation.
- Pas de range check propre : un mur à 100 m peut "occluder" un point au premier plan via la falloff, mais sans raison physique.

### Grass
Le vertex shader `files/shaders/compatibility/groundcover.vert` **fait déjà** :
- `groundcoverDisplacement()` (lignes 62-102) : 4 harmoniques sin/cos sur `osg_SimulationTime`, paramétré par `windSpeed`.
- `STOMP` : poussée par les pas du joueur, 3 niveaux d'intensité, optionnellement sensible à la hauteur.
- Direction du vent **hardcodée** à `vec2(1.0)` (ligne 64) — pas modulable, pas raccord avec la météo du jeu.

Limites :
- Direction de vent constante, pas variable selon zone / météo / heure.
- Pas de gust (rafales) — seulement des harmoniques périodiques pures.
- Aucune variation par-blade (toutes les touffes oscillent en phase si le worldpos ne décale pas assez).
- Pas d'animation fluide d'arrivée d'un coup de vent ; l'amplitude saute si on change `windSpeed` runtime.

## SSAO — plan en 3 phases

### Phase A1 — extraire en `.omwfx` standalone — **DONE** (commit `0f35c61f7f`)
- `files/data/shaders/ssao.omwfx` créé, `pass_normals = true`.
- Logique horizon-AO copiée depuis `internal_raycast.omwfx`.
- Sortie multiplie `omw_GetLastShader(uv)` par AO.

### Phase A2 — pipeline 3 passes — **DONE** (commit `0ee4c98d89`)
- `ao_raw` → R8 quarter-res, range check, hemisphere 2-tap.
- `ao_blur` → 5×5 bilateral cross blur depth-weighted.
- `main` → composite scene × blurredAO.
- 2 fix de syntaxe omwfx découverts pendant l'intégration: top-level `//` interdit (commit `e1f8582b2b`) et `internal_format=r8` non supporté → `red`.

### Phase A3 — paramétrage utilisateur — **DONE de facto**
- Le panneau post-process auto-discover les `uniform_*` du `.omwfx` (Radius / Strength / Samples / Power / Bilateral Blur / Blur Depth Sigma exposés).
- Le toggle global "internal_raycast SSAO vs standalone" se fait via le `uSsaoStrength=0` du raycast + ajout du standalone à la chain.

### Phase A4 — qualité visuelle (non-fait, à débattre)
Constat utilisateur: l'effet SSAO, même bien câblé, est peu perceptible visuellement à `uRadius=80, uStrength=1`. Causes probables:
- Pas de noise texture rotation par-pixel — variance de samples pas amortie.
- Range check trop restrictive sur les arêtes lointaines.
- AO multiplié au stade composite, après tone-map → contrastes écrasés.
**Pistes** (~4 h):
- Pré-multiplier l'AO sur la couleur AVANT le tone-map (intégrer le composite dans `internal_raycast.omwfx` au lieu d'un pass séparé en aval).
- Ajouter une 4×4 blue-noise texture pour la rotation.
- Exposer un slider "AO darkening" non-linéaire (`pow(ao, gamma)` côté composite, pas seulement le `uPower` côté raw).
- A/B contre un GTAO Reference qui shade en hémisphère pleine et compare visuellement.

**Estimation SSAO restante** : Phase A4 si on veut un effet vraiment visible.

## Grass — plan en 3 phases

### Phase B1 — direction de vent dynamique (1 commit, ~2 h)
- Promouvoir `windDirection` en uniform (`uniform vec2 uWindDirection`) au lieu du `vec2(1.0)` hardcodé.
- Côté C++ : un nouveau `osg::Uniform` updaté dans `MWWorld::Weather` ou `MWRender::SkyManager` à chaque frame, dérivé de la direction du vent météo (l'ESM expose déjà `Weather.windSpeed`, étendre à un vector).
- Smooth la direction (low-pass IIR) pour éviter un flip brutal au changement de météo : `windDir = mix(windDir, target, dt * 0.2)`.
- **Critère** : herbe penche vers le sud sous tempête sud, vers l'ouest sous vent d'ouest ; transitions fluides.

### Phase B2 — rafales (gusts) procédurales (1 commit, ~2 h)
- Ajouter une 5e harmonique basse fréquence avec amplitude modulée par un bruit 2D `noise(worldpos.xy * 0.001 + uTime * 0.05)`. Crée des "vagues" de vent qui traversent les champs.
- Le `windSpeed` instantané = `baseSpeed * (1.0 + gustNoise * gustStrength)`. Variations 30-60% sur quelques secondes.
- Garder la cohérence du worldpos pour que deux touffes adjacentes voient la même rafale (look "vent qui passe sur le champ").
- **Critère** : au repos visuel, herbe statique ; sous vent fort, vagues de poussée bien lisibles ; pas de flickering haute fréquence.

### Phase B3 — bend par-blade hauteur-dépendant (1 commit, ~2 h)
- Le code actuel scale par `clamp(0.02 * h, 0.0, 1.0)` — linéaire et borné. Remplacer par une courbe quadratique `h*h * 0.0004` qui colle à la physique d'une tige (le sommet bouge bien plus que la racine).
- Ajouter un offset de phase aléatoire par-instance : utiliser `gl_InstanceID` ou un hash de `worldpos.xy` pour décaler la phase, sinon toutes les touffes oscillent à l'unisson.
- Optionnel : per-blade stiffness — un bruit pré-baked sur la touffe, lu dans le mesh comme un attribut, pour que certaines touffes (ex. broussailles vs hautes herbes) plient moins.
- **Critère** : champ d'herbe vu de loin a l'air d'avoir un mouvement organique, pas un balayage uniforme.

**Estimation grass** : ~6 h, 3 commits indépendants.

## Risques transverses
- **Performance SSAO** : le bilateral blur + 8 samples hémisphère coûte ~1-2 ms à 1080p sur GPU intégré. Garder l'option de samples=4 pour le low-spec.
- **Compat shader profile** : OpenMW supporte des profils GLSL anciens via `compatibility/`. Si on cible un sample noise texture, vérifier qu'on a un sampler2D supplémentaire libre dans le state. Sinon hash procédural en lieu et place.
- **Conflit avec PBR pipeline** : la PBR de la branche utilise `_n.dds` / `_h.dds`. Le SSAO standalone consomme `omw_GetNormals` qui vient du gbuffer normals — déjà alimenté par le PBR shader. Vérifier que `pass_normals = true` reçoit bien les normales bumpées et pas celles de la geo lisse.

## Estimation totale combinée
~12 h, 6 commits indépendants. SSAO et Grass peuvent avancer en parallèle (zéro chevauchement de fichiers).
