#!/bin/bash
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.0.0"
ARCH="arm64"
PKG="means-arms-zero_${VERSION}_${ARCH}.deb"
BUILD_DIR="$DIR/build"
STAGE="$BUILD_DIR/deb-stage"
OUT="$BUILD_DIR/$PKG"

cmake -S "$DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j"$(nproc)"
ctest --test-dir "$BUILD_DIR" --output-on-failure

rm -rf "$STAGE"
mkdir -p "$STAGE/DEBIAN" \
         "$STAGE/usr/bin" \
         "$STAGE/usr/share/applications" \
         "$STAGE/usr/share/APPLaunch/applications" \
         "$STAGE/usr/share/APPLaunch/share/images"

install -m 755 "$BUILD_DIR/means-arms-zero" "$STAGE/usr/bin/means-arms-zero"
strip "$STAGE/usr/bin/means-arms-zero" 2>/dev/null || true

install -m 644 "$DIR/means-arms-zero.png" "$STAGE/usr/share/APPLaunch/share/images/means-arms-zero.png"

cat > "$STAGE/usr/share/APPLaunch/applications/means-arms-zero.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Means Arms
Comment=Reloading & Ballistics
Exec=/usr/bin/means-arms-zero
Icon=/usr/share/APPLaunch/share/images/means-arms-zero.png
Terminal=false
Categories=Utility;
EOF
chmod 644 "$STAGE/usr/share/APPLaunch/applications/means-arms-zero.desktop"
cp "$STAGE/usr/share/APPLaunch/applications/means-arms-zero.desktop" "$STAGE/usr/share/applications/means-arms-zero.desktop"

INSTALLED_KB="$(du -sk "$STAGE/usr" | awk '{print $1}')"

cat > "$STAGE/DEBIAN/control" <<EOF
Package: means-arms-zero
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: ${ARCH}
Maintainer: Jaren <jaren@omarchy.me>
Installed-Size: ${INSTALLED_KB}
Depends: libc6 (>= 2.38), libstdc++6 (>= 13.1), libsdl2-2.0-0, libsdl2-ttf-2.0-0
Description: Means Arms Ammo reloading & ballistics app for CardputerZero
 Native 320x170 reloading loadbook, cost/savings calculator, and ballistic
 drop solver compatible with Means Arms Ammo Android JSON backups.
EOF

if command -v dpkg-deb >/dev/null 2>&1; then
    dpkg-deb --root-owner-group --build "$STAGE" "$OUT"
else
    TMP_AR="$BUILD_DIR/deb-tmp"
    rm -rf "$TMP_AR"
    mkdir -p "$TMP_AR"
    printf '2.0\n' > "$TMP_AR/debian-binary"
    tar --owner=0 --group=0 -czf "$TMP_AR/control.tar.gz" -C "$STAGE/DEBIAN" .
    tar --owner=0 --group=0 -czf "$TMP_AR/data.tar.gz" -C "$STAGE" ./usr
    rm -f "$OUT"
    (cd "$TMP_AR" && ar rcs "$OUT" debian-binary control.tar.gz data.tar.gz)
    rm -rf "$TMP_AR"
fi

echo "Built $OUT"
ls -lh "$OUT"
