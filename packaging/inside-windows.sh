#!/usr/bin/env bash
# Runs in the build image (build-windows.sh): cross-compiles the program with
# llvm-mingw and Qt for Windows and gathers the distribution folder: the
# program, the Qt plugins it loads and every DLL they import from Qt and the
# llvm-mingw runtime (libc++, libunwind). The C runtime (UCRT) is part of
# Windows 10 and 11.
source "$(dirname "$0")/common.sh"
HOST_QT="$QT_ROOT/$QT_VERSION/gcc_64"
QT="$QT_ROOT/$QT_VERSION/llvm-mingw_64"
LLVM=/opt/llvm-mingw
WORK="$APP_DIR/build/docker-windows"
NAME="biosignal-analyzer-$VERSION-windows-x86_64"

cmake -S "$APP_DIR" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$APP_DIR/packaging/windows-toolchain.cmake" \
    -DCMAKE_PREFIX_PATH="$QT" -DQT_HOST_PATH="$HOST_QT" \
    -DEIGEN3_INCLUDE_DIR=/usr/include/eigen3
cmake --build "$WORK/build"

OUT="$DIST_DIR/$NAME"
rm -rf "$OUT" "$OUT.zip"
mkdir -p "$OUT"
cp "$WORK/build/biosignal_analyzer.exe" "$OUT/"
# Plugins: the window system, the Windows 11 style, image formats, SVG icons.
for plugin in platforms/qwindows.dll styles/qmodernwindowsstyle.dll \
              imageformats/qgif.dll imageformats/qico.dll imageformats/qjpeg.dll \
              imageformats/qsvg.dll iconengines/qsvgicon.dll; do
    mkdir -p "$OUT/plugins/$(dirname "$plugin")"
    cp "$QT/plugins/$plugin" "$OUT/plugins/$plugin"
done
# DLLs: the closure of the imports that are found in Qt or llvm-mingw; the
# rest (KERNEL32, USER32, UCRT...) belong to Windows.
search=("$QT/bin" "$LLVM/x86_64-w64-mingw32/bin")
queue=("$OUT/biosignal_analyzer.exe")
while IFS= read -r -d '' plugin; do queue+=("$plugin"); done < <(find "$OUT/plugins" -name '*.dll' -print0)
while [ ${#queue[@]} -gt 0 ]; do
    file=${queue[0]}
    queue=("${queue[@]:1}")
    for dll in $("$LLVM/bin/llvm-objdump" -p "$file" | sed -n 's/^ *DLL Name: //p'); do
        [ -e "$OUT/$dll" ] && continue
        for dir in "${search[@]}"; do
            if [ -f "$dir/$dll" ]; then
                cp "$dir/$dll" "$OUT/"
                queue+=("$OUT/$dll")
                break
            fi
        done
    done
done
mkdir -p "$OUT/translations"
cp "$QT/translations/qtbase_ru.qm" "$OUT/translations/"
printf '[Paths]\nPrefix = .\nPlugins = plugins\nTranslations = translations\n' > "$OUT/qt.conf"
copy_runtime_data "$OUT"
(cd "$DIST_DIR" && bsdtar -a -cf "$NAME.zip" "$NAME")
