#!/usr/bin/env bash
# Runs in the build image (build-linux.sh): builds the program with Qt for
# Linux and gathers the distribution folder with linuxdeploy.
source "$(dirname "$0")/common.sh"
QT="$QT_ROOT/$QT_VERSION/gcc_64"
WORK="$APP_DIR/build/docker-linux"
NAME="biosignal-analyzer-$VERSION-linux-x86_64"

# OpenGL LEGACY: the program links the same libGL.so.1 as Qt Gui does. With
# the default GLVND libraries (libOpenGL, libGLX) the linker records them as
# needed for Qt Gui, and libOpenGL (libopengl0) is missing on many systems.
cmake -S "$APP_DIR" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT" \
    -DOpenGL_GL_PREFERENCE=LEGACY
cmake --build "$WORK/build"

APPDIR="$WORK/AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
cp "$WORK/build/biosignal_analyzer" "$APPDIR/usr/bin/"
# The xcb platform plugin is deployed always; Wayland, and offscreen for a
# start without a display, are extra.
export QMAKE="$QT/bin/qmake" LD_LIBRARY_PATH="$QT/lib"
export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so;libqwayland-egl.so;libqoffscreen.so"
linuxdeploy --appdir "$APPDIR" --executable "$APPDIR/usr/bin/biosignal_analyzer" \
    --desktop-file "$APP_DIR/packaging/linux/biosignal-analyzer.desktop" \
    --icon-file "$APP_DIR/packaging/linux/biosignal-analyzer.svg" \
    --plugin qt

# The distribution is the usr tree of the AppDir: bin (program, qt.conf),
# lib, plugins, translations; RPATH and qt.conf are relative.
OUT="$DIST_DIR/$NAME"
rm -rf "$OUT" "$OUT.tar.xz"
mkdir -p "$DIST_DIR"
mv "$APPDIR/usr" "$OUT"
rm -rf "$OUT/share"

# The Qt print plugin is not needed (Qt Print Support comes with QCustomPlot,
# the program does not print); it would bring libcups with its network and
# Kerberos libraries. Only the libraries that the program and the remaining
# plugins need are kept, and libdbus, which Qt loads at run time.
rm -rf "$OUT/plugins/printsupport"
declare -A keep=()
queue=("$OUT/bin/biosignal_analyzer" "$OUT/lib/libdbus-1.so.3")
while IFS= read -r -d '' plugin; do queue+=("$plugin"); done < <(find "$OUT/plugins" -name '*.so' -print0)
while [ ${#queue[@]} -gt 0 ]; do
    file=${queue[0]}
    queue=("${queue[@]:1}")
    keep[$(basename "$file")]=1
    for lib in $(objdump -p "$file" | sed -n 's/^ *NEEDED *//p'); do
        if [ -f "$OUT/lib/$lib" ] && [ -z "${keep[$lib]:-}" ]; then
            keep[$lib]=1
            queue+=("$OUT/lib/$lib")
        fi
    done
done
for lib in "$OUT"/lib/*; do
    [ -n "${keep[$(basename "$lib")]:-}" ] || rm -f "$lib"
done
copy_runtime_data "$OUT/bin"
ln -s bin/biosignal_analyzer "$OUT/biosignal-analyzer"
tar -C "$DIST_DIR" -cf - "$NAME" | xz -T0 -9 > "$OUT.tar.xz"
