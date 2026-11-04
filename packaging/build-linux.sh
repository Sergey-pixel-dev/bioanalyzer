#!/usr/bin/env bash
# Linux distribution: dist/biosignal-analyzer-<version>-linux-x86_64.tar.xz.
# Built in the Ubuntu 22.04 image, so it runs on Ubuntu 22.04, Debian 12 and
# every newer GNU/Linux for x86-64.
source "$(dirname "$0")/common.sh"
ensure_qt_linux
ensure_image
in_container bash packaging/inside-linux.sh
echo "== $(ls -sh "$DIST_DIR"/biosignal-analyzer-"$VERSION"-linux-x86_64.tar.xz)"
