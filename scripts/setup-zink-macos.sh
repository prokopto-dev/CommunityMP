#!/usr/bin/env bash
# Setup Mesa + Zink + MoltenVK on Apple Silicon macOS so OpenMW (built against
# this stack) can use the OpenGL 4.6 compatibility profile and the hardware
# tessellation pipeline shipped with the 'tessellation = true' setting.
#
# This installs into $HOME/mesa-native by default; nothing in /usr or
# /opt/homebrew is overwritten. The script is idempotent: re-running picks up
# from where a previous run left off.
#
# After it finishes, source the printed env block before launching OpenMW (or
# before re-running cmake to rebuild OpenMW against this libGL).

set -euo pipefail

PREFIX="${PREFIX:-$HOME/mesa-native}"
MESA_REF="${MESA_REF:-main}"        # tag or branch to check out; pin to a release tag if you want stability
JOBS="${JOBS:-$(sysctl -n hw.ncpu)}"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "This script is for macOS only." >&2
    exit 1
fi
if [[ "$(uname -m)" != "arm64" ]]; then
    echo "Warning: not running on Apple Silicon (arm64). Continuing anyway." >&2
fi
if ! command -v brew >/dev/null 2>&1; then
    echo "Homebrew is required. Install from https://brew.sh first." >&2
    exit 1
fi

echo "==> Installing Homebrew dependencies"
brew install --quiet \
    meson ninja python3 bison flex llvm \
    glslang spirv-tools molten-vk \
    pkg-config libxml2

# Python templating module Mesa needs at build time.
python3 -m pip install --user --quiet mako || true

mkdir -p "$PREFIX"
SRC="$PREFIX/src"
mkdir -p "$SRC"

if [[ ! -d "$SRC/mesa/.git" ]]; then
    echo "==> Cloning Mesa ($MESA_REF)"
    git clone --depth 1 --branch "$MESA_REF" https://gitlab.freedesktop.org/mesa/mesa.git "$SRC/mesa"
fi

cd "$SRC/mesa"

echo "==> Configuring Mesa with Zink for macOS"
# brew sh exposes Homebrew's bison/flex/llvm in PATH for the build.
brew sh -c "
    set -e
    cd '$SRC/mesa'
    if [[ ! -d builddir ]]; then
        meson setup builddir \
            -Dprefix='$PREFIX' \
            -Dgallium-drivers=zink \
            -Dvulkan-drivers= \
            -Dplatforms=macos \
            -Dbuildtype=release \
            -Dosmesa=false \
            -Dglx=disabled \
            -Degl=enabled \
            -Dgles1=disabled \
            -Dgles2=disabled \
            -Dshared-glapi=enabled \
            -Dgallium-extra-hud=false
    fi
    echo '==> Building Mesa (this takes a while)'
    meson compile -C builddir -j$JOBS
    echo '==> Installing to $PREFIX'
    meson install -C builddir
"

cat <<EOF

==============================================================================
Mesa + Zink installed at: $PREFIX

To use this stack at runtime, export the following before launching OpenMW:

    export DYLD_LIBRARY_PATH="$PREFIX/lib:\${DYLD_LIBRARY_PATH:-}"
    export MESA_LOADER_DRIVER_OVERRIDE=zink
    export MESA_GL_VERSION_OVERRIDE=4.6
    export MESA_GLSL_VERSION_OVERRIDE=460
    export VK_ICD_FILENAMES="\$(brew --prefix molten-vk)/share/vulkan/icd.d/MoltenVK_icd.json"

To rebuild OpenMW against this libGL (recommended — Apple's
OpenGL.framework will otherwise be picked up at link time):

    cd <openmw checkout>
    rm -rf build
    cmake -B build -G Ninja \\
        -DOPENGL_gl_LIBRARY="$PREFIX/lib/libGL.dylib" \\
        -DOPENGL_INCLUDE_DIR="$PREFIX/include" \\
        -DCMAKE_BUILD_TYPE=Release
    ninja -C build openmw

Then enable the hardware path in your settings.cfg:

    [Terrain]
    tessellation = true
    tessellation max level = 8
    tessellation displacement scale = 32.0

Verify by running OpenMW once with the env vars set and checking the log:

    grep -i "OpenGL Version" "\$HOME/Library/Logs/OpenMW/openmw.log"

It should report something like 'OpenGL Version: 4.6 (Compatibility Profile)
Mesa <version> (zink, MoltenVK <version>)'.
==============================================================================
EOF
