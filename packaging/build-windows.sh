#!/usr/bin/env bash
# Windows distribution: dist/biosignal-analyzer-<version>-windows-x86_64.zip.
# Cross-compiled with llvm-mingw in the build image, for Windows 10 and 11.
source "$(dirname "$0")/common.sh"
ensure_qt_linux    # host tools of the cross build
ensure_qt_windows
ensure_image
in_container bash packaging/inside-windows.sh
echo "== $(ls -sh "$DIST_DIR"/biosignal-analyzer-"$VERSION"-windows-x86_64.zip)"
