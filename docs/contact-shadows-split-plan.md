# Plan — extraire les contact shadows de `internal_raycast.omwfx`

## Contexte

`files/data/shaders/internal_raycast.omwfx` est un méga-shader monolithique (~1100 lignes) qui contient :

- contact shadows (raymarche écran-espace vers le soleil)
- SSAO
- SSR
- god rays
- atmospheric scattering
- volumetric fog / clouds
- eye adaptation
- wetness, sun halo, parallax tuning

Ça rend l'itération douloureuse — chaque modif fait recompiler 1100 lignes de GLSL et touche des passes qui n'ont rien à voir. On a déjà extrait SSAO en `ssao.omwfx`. Étape suivante : **contact shadows en `contactshadows.omwfx`**.

## Code actuel (lignes 320-388 de `internal_raycast.omwfx`)

```glsl
float contactShadow(vec2 uv)
{
    if (!uContactShadows) return 1.0;
    vec3 sunVS = normalize((omw.viewMatrix * vec4(omw.sunPos.xyz, 0.0)).xyz);
    if (sunVS.z >= 0.0) return 1.0;

    vec3 origin = viewPosFromUV(uv);
    vec3 n = omw_GetNormals(uv);
    origin += n * 4.0; // bias

    float worldLength = uContactShadowsLength * 600.0;
    vec3 endVS = origin + sunVS * worldLength;
    vec4 clipS = omw_ProjectionMatrix() * vec4(origin, 1.0);
    vec4 clipE = omw_ProjectionMatrix() * vec4(endVS, 1.0);
    vec2 ssStart = (clipS.xy / clipS.w) * 0.5 + 0.5;
    vec2 ssEnd   = (clipE.xy / clipE.w) * 0.5 + 0.5;

    // Hash dither anchored on world position (pas sur gl_FragCoord).
    vec3 originWS = (omw.invViewMatrix * vec4(origin, 1.0)).xyz;
    float dither = fract(sin(dot(originWS.xyz, ...)) * ...);

    // Marche le long du segment écran, compare la profondeur sample vs ray.
    for (int i = 1; i <= steps; ++i) { ... }
    return 1.0 - occluded * 0.7;
}
```

Uniforms consommés :
- `uContactShadows` (bool, on/off)
- `uContactShadowsSteps` (int, qualité)
- `uContactShadowsLength` (float, portée monde)
- `uContactShadowsThickness` (float, fenêtre d'occlusion)

Inputs scène consommés :
- `omw.viewMatrix`, `omw.invViewMatrix`, `omw_ProjectionMatrix()` — caméra (NÉCESSAIRE pour la projection écran-espace, voir note plus bas)
- `omw.sunPos` — direction soleil monde
- `omw_GetNormals(uv)` — G-buffer normales
- `omw_GetDepth(uv)` (via `viewPosFromUV`) — G-buffer profondeur

## Demande utilisateur — "sans position du player ni caméra, par rapport au sun"

**Note importante sur la caméra** : un raymarche écran-espace **ne peut pas** être totalement indépendant de la caméra — la projection écran ↔ vue requiert intrinsèquement la matrice de projection. Ce qu'on peut faire :

1. **Direction de l'ombre = direction du soleil monde**, pas dépendante de la position du joueur (déjà le cas via `omw.sunPos`).
2. **Pas de dépendance à `playerPos`** — déjà le cas (le shader actuel ne lit jamais `omw.eyePos`).
3. **Dither monde-anchré** — déjà le cas (lignes 359-361).
4. **Atténuation/portée selon l'éclairage actif** — exposer `omw.sunVis` pour fader l'effet hors plein-jour, et ajouter un facteur `omw.ambientColor` ou similaire pour gérer la nuit.

Le seul "couplage caméra" résiduel est la **projection** (nécessaire pour sampler le depth buffer). Ça reste un effet écran-espace par nature ; le rendre purement world-space exigerait des shadow maps cascadées (chantier ×10 plus gros).

## Plan d'extraction

### Phase 1 — Création de `files/data/shaders/contactshadows.omwfx` — **DONE** (commit `44169d0350` + sky-skip fix `cbbc1aed76`)

Nouveau fichier, structure identique à `ssao.omwfx` :

```glsl
uniform_int uSteps {
    default = 38;
    min = 8;
    max = 128;
    display_name = "Contact Shadow Steps";
}

uniform_float uLength {
    default = 0.05;
    min = 0.01;
    max = 0.5;
    display_name = "Contact Shadow Length (world units / 600)";
}

uniform_float uThickness {
    default = 0.025;
    min = 0.005;
    max = 0.1;
    display_name = "Contact Shadow Thickness";
}

uniform_float uStrength {
    default = 0.7;
    min = 0.0;
    max = 1.0;
    display_name = "Contact Shadow Strength";
    description = "Atténuation finale appliquée sur la zone occluée (0 = pas d'ombre, 1 = noir total).";
}

uniform_bool uFadeWithSun {
    default = true;
    display_name = "Fade With Sun Visibility";
    description = "Réduit l'effet quand le soleil est masqué par les nuages ou hors-zénith. Utilise omw.sunVis.";
}

shared {
    vec3 viewPosFromUV(vec2 uv) {
        float d = omw_GetDepth(uv);
        #if (OMW_REVERSE_Z == 1)
        float ndcZ = 1.0 - d;
        #else
        float ndcZ = d * 2.0 - 1.0;
        #endif
        vec4 ndc = vec4(uv * 2.0 - 1.0, ndcZ, 1.0);
        vec4 view = omw_InvProjectionMatrix() * ndc;
        return view.xyz / view.w;
    }
}

render_target RT_Shadows {
    width_ratio = 1.0;
    height_ratio = 1.0;
    internal_format = red;
    source_type = unsigned_byte;
    source_format = red;
    mipmaps = false;
    min_filter = linear;
    mag_filter = linear;
}

fragment cshadow_raw(target=RT_Shadows) {
    omw_In vec2 omw_TexCoord;

    void main() {
        // Direction soleil en view space.
        vec3 sunVS = normalize((omw.viewMatrix * vec4(omw.sunPos.xyz, 0.0)).xyz);
        if (sunVS.z >= 0.0) {  // soleil sous l'horizon
            omw_FragColor = vec4(1.0);
            return;
        }

        // Atténuation par visibilité soleil (nuages / nuit).
        float sunFade = uFadeWithSun ? clamp(omw.sunVis, 0.0, 1.0) : 1.0;
        if (sunFade < 0.05) {
            omw_FragColor = vec4(1.0);
            return;
        }

        // ... raymarche identique à internal_raycast lines 332-384 ...
        float occluded = 0.0;
        // (loop)
        omw_FragColor = vec4(1.0 - occluded * uStrength * sunFade, 0.0, 0.0, 1.0);
    }
}

fragment main(rt1=RT_Shadows) {
    omw_In vec2 omw_TexCoord;

    void main() {
        vec4 col = omw_GetLastShader(omw_TexCoord);
        float shadow = omw_Texture2D(RT_Shadows, omw_TexCoord).r;
        omw_FragColor = vec4(col.rgb * shadow, col.a);
    }
}

technique {
    description = "Standalone screen-space contact shadows toward the sun. Driven by omw.sunPos + omw.sunVis, independent of camera/player position other than what's required for screen-space projection.";
    author = "OpenMW Boost";
    version = "1.0";
    passes = cshadow_raw, main;
    pass_normals = true;
}
```

**Critère** : visuellement identique à `uContactShadows=true` dans `internal_raycast` à paramètres équivalents, fadait correctement à l'aube/crépuscule via `omw.sunVis`.

### Phase 2 — Documentation in-raycast pour éviter le double effet — **DONE**

- Le `display_name` et `description` du `uContactShadows` dans `internal_raycast.omwfx` annoncent maintenant le standalone et indiquent au user de désactiver l'in-raycast quand `contactshadows` est dans la chain.
- Pas de gate dynamique GLSL (impossible sans nouveau pipeline d'introspection de chain) — c'est UI-driven : le user décoche manuellement la case dans le panneau F2.
- Compat saves intacte — les `uContactShadows*` uniforms gardent leurs valeurs par défaut.

### Phase 3 — Refacto pour purer world-space + lighting (~2 h, optionnel)

Si l'utilisateur veut vraiment "indépendant de la caméra" au sens VRAI, deux options :

**Option A — Reconstruction world-space pure** :
- `viewPosFromUV` reste (la projection écran ↔ vue est inévitable pour sampler le depth)
- TOUT le reste du raymarche se fait en **world space** : converter `origin` en world via `omw.invViewMatrix`, générer la marche dans le repère monde, puis re-projeter chaque pas en écran pour sampler la profondeur.
- Coût : 2 transformations matricielles supplémentaires par pas. Acceptable à 38 pas.
- Bénéfice : le code lit comme "je marche du point P vers le soleil S, en monde", pas "je projette en écran et je marche le segment 2D" — plus lisible, plus modifiable.

**Option B — Ajout de lights actives** :
- Étendre le shader pour itérer sur les lights actives proches (torches, sorts) et accumuler leur contribution.
- OpenMW expose `omw.lightCount`, `omw.lights[i]` (à confirmer côté `components/fx/stateupdater.hpp`).
- Pour chaque light, refaire un raymarche similaire mais avec direction = `light.pos - origin` au lieu de `sunDir`.
- Coût : ×N samples par pixel où N = nombre de lights. Cap à 4 lights max sinon insupportable.

### Phase 4 — UI panneau post-process (gratuit)

`uniform_*` du `.omwfx` sont auto-discovered — tous les sliders apparaissent dans le panneau F2 sans code C++ supplémentaire.

## Estimation totale

- Phase 1 : ~1 h
- Phase 2 : ~30 min
- Phase 3 : ~2 h (optionnel — propres world-space ou lights actives)
- **Total minimum** : ~1.5 h pour un standalone fonctionnel équivalent.

## Risques

| Risque | Détail | Mitigation |
|---|---|---|
| Doublon avec `internal_raycast` | Si l'utilisateur a `uContactShadows=true` ET `contactshadows` dans la chain → ombres × 2 | Documenter clairement, ajouter une note dans le `description` du technique. |
| Performance | Ombre re-faite après le raycast monolithique = doubler le coût des passes contact-shadow | C'est une trade-off ; le standalone sert à itérer / désactiver le mégashader, pas à le compléter. Pour le run de production, soit l'un soit l'autre. |
| Cohérence visuelle | `internal_raycast` tone-map à un endroit, le standalone n'a pas le même contexte | Dans le pass de composite, multiplier sur `omw_GetLastShader` (ce que fait déjà SSAO standalone). |
