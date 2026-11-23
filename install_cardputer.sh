#!/bin/bash
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"

if ! dpkg -s libsdl2-2.0-0 libsdl2-ttf-2.0-0 >/dev/null 2>&1; then
    echo "Installing SDL2 runtime libraries..."
    sudo apt-get update
    sudo apt-get install -y libsdl2-2.0-0 libsdl2-ttf-2.0-0
fi

BIN="$DIR/means-arms-zero"
if [ ! -x "$BIN" ] && [ -x "$DIR/build/means-arms-zero" ]; then
    BIN="$DIR/build/means-arms-zero"
fi

if [ ! -x "$BIN" ]; then
    echo "Building means-arms-zero..."
    sudo apt-get install -y g++ cmake pkg-config libsdl2-dev libsdl2-ttf-dev
    cmake -S "$DIR" -B "$DIR/build"
    cmake --build "$DIR/build" -j"$(nproc)"
    BIN="$DIR/build/means-arms-zero"
fi

sudo install -m 755 "$BIN" /usr/local/bin/means-arms-zero

ICON_DEST=""
if [ -f "$DIR/means-arms-zero.png" ] && [ -d /usr/share/APPLaunch/share/images ]; then
    ICON_DEST="/usr/share/APPLaunch/share/images/means-arms-zero.png"
    sudo install -m 644 "$DIR/means-arms-zero.png" "$ICON_DEST"
fi

mkdir -p "$HOME/.local/share/applications"
cat > "$HOME/.local/share/applications/means-arms-zero.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Means Arms
Comment=Reloading & Ballistics for CardputerZero
Exec=/usr/local/bin/means-arms-zero
${ICON_DEST:+Icon=$ICON_DEST}
Terminal=false
Categories=Utility;
EOF

if [ -d /usr/share/APPLaunch/applications ]; then
    sudo install -m 644 "$HOME/.local/share/applications/means-arms-zero.desktop" /usr/share/APPLaunch/applications/means-arms-zero.desktop
fi

echo "Installed! Launch 'Means Arms' from APPLaunch or run 'means-arms-zero'."
