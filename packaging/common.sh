# Sourced by build-linux.sh and build-windows.sh.
#  - Qt: ready-made binaries (no compilation) downloaded by aqtinstall into
#    build/qt/<version>/: gcc_64 is Qt for Linux, the Linux build and the
#    host tools (moc, uic, rcc, lrelease) of the Windows cross build;
#    llvm-mingw_64 is Qt for Windows built with llvm-mingw (UCRT, libc++).
#    The MinGW GCC kit of Qt uses the old msvcrt.dll, which the current MinGW
#    toolchains no longer target, so it is not used.
#  - The build image (Dockerfile): compilers and deployment tools.
# Everything already present is not downloaded again.

set -euo pipefail

QT_VERSION=6.8.3
AQT="uvx --from aqtinstall==3.3.0 aqt"
IMAGE=biosignal-build:22.04
APP_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
QT_ROOT="$APP_DIR/build/qt"
DIST_DIR="$APP_DIR/dist"
VERSION=$(sed -n 's/^project([^ ]* VERSION \([0-9.]*\).*/\1/p' "$APP_DIR/CMakeLists.txt")

# ensure_qt <host> <arch> <dir> <archives...>
ensure_qt()
{
    local host=$1 arch=$2 dir=$3
    shift 3
    if [ -d "$QT_ROOT/$QT_VERSION/$dir/lib/cmake/Qt6" ]; then
        return 0
    fi
    echo "== Qt $QT_VERSION $arch: downloading into $QT_ROOT"
    $AQT install-qt "$host" desktop "$QT_VERSION" "$arch" --outputdir "$QT_ROOT" --archives "$@"
}

ensure_qt_linux()
{
    ensure_qt linux linux_gcc_64 gcc_64 icu qtbase qtsvg qttools qttranslations qtwayland
}

ensure_qt_windows()
{
    ensure_qt windows win64_llvm_mingw llvm-mingw_64 qtbase qtsvg qttools qttranslations
}

ensure_image()
{
    if ! docker image inspect "$IMAGE" > /dev/null 2>&1; then
        echo "== Build image $IMAGE: building from packaging/Dockerfile"
        docker build -t "$IMAGE" "$APP_DIR/packaging"
    fi
}

# in_container <command...>: runs in the build image as the current user,
# with the application folder mounted at the same path (the Qt kits keep
# their paths and the files created belong to the user).
in_container()
{
    docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
        -e QT_VERSION="$QT_VERSION" -e VERSION="$VERSION" \
        -v "$APP_DIR:$APP_DIR" -w "$APP_DIR" "$IMAGE" "$@"
}

# copy_runtime_data <program folder>: the files the program reads from its
# own folder — the AI worker with the model modules and the task descriptions.
copy_runtime_data()
{
    local dest=$1
    rm -rf "$dest/ai_worker" "$dest/tasks"
    tar -C "$APP_DIR" --exclude=__pycache__ -cf - ai_worker tasks | tar -C "$dest" -xf -
}
