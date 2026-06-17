# Génération de textures PBR pour OpenMW (upscaling + parallax)

Pipeline pour convertir les diffuses Morrowind originales en pack PBR
(diffuse 4x + normal + height + specular) consommé par le shader OpenMW
en mode parallax. Tout vit côté `scripts/`.

## Contenu du pack généré

Pour chaque source `tx_foo.tga`, on produit dans le dossier overlay :

| Fichier | Contenu | Conso OpenMW |
|---|---|---|
| `tx_foo.tga` (et `.dds`) | Diffuse upscalée 4x | TU0 |
| `tx_foo_n.tga`/`.dds`/`.png` | Normal map (RGB) | TU1, autoload via `_n` pattern |
| `tx_foo_h.tga`/`.dds`/`.png` | Height map (grayscale) | non utilisée directement |
| `tx_foo_s.tga`/`.dds`/`.png` | Specular / smoothness (legacy alias) | autoload si `specular map pattern = _s` |
| `tx_foo_spec.tga`/`.dds`/`.png` | Specular / smoothness | TU2, autoload via défaut `_spec` |
| `tx_foo_nh.tga`/`.dds`/`.png` | Normal+height combiné (RGB+A) | **path parallax** |

OpenMW active le parallax mapping uniquement quand il trouve un `_nh`
(pattern `setNormalHeightMapPattern`, défaut `_nh`). Sans `_nh`, le
shader `objects` ignore l'uniform `parallaxScale`.

## Prérequis

- `REPLICATE_API_TOKEN` — créer un token sur
  <https://replicate.com/account/api-tokens>. Le pix2pix tourne
  ~$0.001 par appel × 3 sub-modèles (`normal`, `height`, `smoothness`)
  + 1 appel Real-ESRGAN si `--upscale > 1`. Compter $0.005-0.01 par
  texture, ~$0.50 pour 100 textures.
- `pip install replicate Pillow`
- `magick` (ImageMagick) — pour la conversion DDS et le batch PNG→TGA.

## Étapes

### 1. Identifier le dossier source Morrowind

Sur macOS le path par défaut est lu depuis `~/Library/Preferences/openmw/openmw.cfg` :

```
data="/Users/<you>/Library/Application Support/openmw/morrowind-data/Data Files"
```

Les diffuses originales sont dans `Data Files/Textures/` (souvent en
`.tga` ou `.dds` avec préfixe majuscule `Tx_*`).

### 2. Préparer le dossier overlay

Crée un nouveau data-path dans `openmw.cfg` qui surchargera Morrowind :

```
data="/Users/<you>/Library/Application Support/openmw/pbr-overlay/Data Files"
```

Ce path est lu **après** Morrowind dans la chaîne VFS, donc les fichiers
qu'il contient overrident les originaux.

### 3. Générer les PBR

```sh
REPLICATE_API_TOKEN=r8_xxx python3 scripts/generate_pbr_textures.py \
  --input  "/.../morrowind-data/Data Files/Textures" \
  --output "/.../pbr-overlay/Data Files/Textures" \
  --filter 'Tx_wall_stone*.tga' \
  --upscale 4 \
  --dds
```

Drapeaux utiles :
- `--filter '<pattern>'` — glob côté entrée (`Tx*wall*.tga`, `Tx_AC_*.dds`).
- `--upscale 4` — Real-ESRGAN 4x avant le pix2pix. Donne un bien meilleur
  PBR car le model voit plus de détail.
- `--dds` — convertit chaque `.png` de sortie en `.dds` DXT compressé via
  ImageMagick (`dxt1` pour la diffuse, `dxt5` pour le reste).
- `--max N` — limite à N textures pour tester.
- `--dry-run` — liste les inputs sans appeler l'API.
- `--skip-flora` — saute les `flora_*`, `tree_*`, etc. (alpha-test, pas
  pertinent pour le parallax).
- `--skip-transparent` — saute les textures avec masque alpha non trivial.

Le script écrit incrémentalement et skip toute texture déjà générée
(détecte la présence du `_n.png`), donc on peut interrompre et reprendre.

Pour les sources `.tga`/`.dds`/`.bmp`, le script transcode en PNG via
Pillow avant d'envoyer à Replicate (la Replicate API rejette les autres
formats).

### 4. Combiner les `_n` + `_h` en `_nh` (parallax)

Le script Replicate produit `_n` et `_h` en deux fichiers séparés.
OpenMW veut un `_nh` combiné (RGB = normal, alpha = heightmap). Le
helper :

```sh
python3 scripts/combine_nh.py "/.../pbr-overlay/Data Files/Textures"
```

Émet `tx_foo_nh.png`/`.tga`/`.dds` pour chaque paire trouvée, idempotent
(skip si `_nh` déjà à jour).

### 5. Convertir les PNG en TGA

OpenMW préfère le format de la source. Si la NIF référence
`tx_foo.tga`, on veut un `tx_foo.tga` (pas un `.png`) dans l'overlay.
Le script `--dds` produit déjà des `.dds` mais pas de `.tga`. Pour
batch-convertir :

```sh
cd "/.../pbr-overlay/Data Files/Textures/"
for f in *.png; do
  tga="${f%.png}.tga"
  [ -f "$tga" ] || magick "$f" "$tga"
done
```

### 6. Forcer les noms en lowercase

Sur Linux le VFS est case-sensitive. Pour rester portable, normalise
les majuscules initiales :

```sh
cd "/.../pbr-overlay/Data Files/Textures/"
for f in T*; do
  new="${f/#T/t}"
  [ "$f" != "$new" ] && mv "$f" "_tmp_rename" && mv "_tmp_rename" "$new"
done
```

(Le double `mv` via temp est nécessaire sur APFS/HFS+ macOS, qui sont
case-insensitive et refusent un `mv T t` direct.)

## Validation in-game

Avec l'overlay activé dans `openmw.cfg`, lance OpenMW. Sous F1 → `Pick
from world`, clique un mur en pierre. Le pane Material affiche :

```
Diffuse  : textures\tx_wall_stone_01.tga
Normal   : textures\tx_wall_stone_01_nh.tga
Specular : textures\tx_wall_stone_01_s.tga
Heightmap: present  → parallaxScale will work
```

Si "Heightmap: absent", check les settings dans **`~/Library/Preferences/openmw/settings.cfg`** (les options ne sont pas exposées dans le menu in-game, sauf via le pane ImGui "Shader Settings" si activé) :

```
[Shaders]
auto use object normal maps = true
auto use object specular maps = true
parallax mapping = true
normal map pattern = _n
normal height map pattern = _nh
specular map pattern = _spec
```

Et vérifier :
- Le `_nh.tga`/`.dds` est bien dans le dossier overlay
- Le `_spec.tga`/`.dds` aussi (le script écrit `_s` ET `_spec` en alias)
- L'overlay est dans `openmw.cfg` (`data=...`)

## Coûts

Chiffres approximatifs (Replicate, mai 2026) :
- Real-ESRGAN 4x : ~$0.001 par appel, ~3-15s.
- pix2pix (3 sub-modèles : normal + height + smoothness) : 3 × ~$0.001,
  ~5-30s par sub-modèle.
- Total par texture : ~$0.005, 30s à 3min selon la taille source et la
  charge Replicate.

Pour le catalogue complet vanilla Morrowind (~5000 textures pertinentes,
en filtrant flora/menu/UI), compter ~$25-50 et 3-8h de wall-clock.

## Limitations connues

- Le pix2pix tendance à produire des normal maps **plates** (XY proches
  de 128). Le post-process `_sharpen_and_renormalize_normal` du script
  applique un unsharp mask + renormalise Z pour récupérer du contraste.
- Le smoothness/specular est souvent trop blanc (objets trop spéculaires
  par défaut). À ajuster manuellement si besoin via override matériel YAML.
- Real-ESRGAN désature légèrement ; `_match_saturation` réinjecte la
  saturation de la source pour préserver l'art direction Bethesda.
- Les textures avec masque alpha (foliage, grilles métal) traversent mal
  le pipeline ; utiliser `--skip-transparent`.

## Workflow complet (one-liner)

```sh
REPLICATE_API_TOKEN=r8_xxx python3 scripts/generate_pbr_textures.py \
  --input  "/.../morrowind-data/Data Files/Textures" \
  --output "/.../pbr-overlay/Data Files/Textures" \
  --filter 'Tx*wall*.tga' --upscale 4 --dds && \
python3 scripts/combine_nh.py "/.../pbr-overlay/Data Files/Textures" && \
cd "/.../pbr-overlay/Data Files/Textures/" && \
for f in *.png; do tga="${f%.png}.tga"; [ -f "$tga" ] || magick "$f" "$tga"; done && \
for f in T*; do new="${f/#T/t}"; [ "$f" != "$new" ] && mv "$f" "_tmp_rename" && mv "_tmp_rename" "$new"; done
```
