#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_NAME="wacom-cursor"
INSTALL_BIN="$HOME/.local/bin/${BIN_NAME}"
DESKTOP_FILE="$HOME/.local/share/applications/${BIN_NAME}.desktop"
AUTOSTART_FILE="$HOME/.config/autostart/${BIN_NAME}.desktop"

echo "=== Wacom Cursor Overlay – Installation ==="
echo "Projektpfad: $SCRIPT_DIR"
echo ""

# --- Build-Abhängigkeiten prüfen ---
echo "Prüfe Build-Abhängigkeiten …"
DEPS_MISSING=0
for pkg in x11 xfixes xext xrandr cairo libevdev gtk+-3.0 appindicator3-0.1; do
    if ! pkg-config --exists "$pkg" 2>/dev/null; then
        echo "  [!] $pkg fehlt"
        DEPS_MISSING=1
    fi
done

if ! command -v gcc &>/dev/null || ! command -v make &>/dev/null; then
    echo "  [!] gcc oder make fehlt"
    DEPS_MISSING=1
fi

if [ "$DEPS_MISSING" -eq 1 ]; then
    echo ""
    if command -v dnf &>/dev/null; then
        INSTALL_CMD="sudo dnf install -y libX11-devel libXfixes-devel libXext-devel libXrandr-devel cairo-devel libevdev-devel gtk3-devel libappindicator-gtk3-devel gcc make pkgconf"
    elif command -v apt &>/dev/null; then
        INSTALL_CMD="sudo apt install -y libx11-dev libxfixes-dev libxext-dev libxrandr-dev libcairo2-dev libevdev-dev libgtk-3-dev libappindicator3-dev gcc make pkg-config"
    elif command -v pacman &>/dev/null; then
        INSTALL_CMD="sudo pacman -S --noconfirm libx11 libxfixes libxext libxrandr cairo libevdev gtk3 libappindicator-gtk3 gcc make pkgconf"
    else
        echo "Paketmanager nicht erkannt. Bitte manuell installieren:"
        echo "  libx11, libxfixes, libxext, libxrandr, cairo, libevdev, gtk3, libappindicator-gtk3"
        exit 1
    fi
    echo "Installationsbefehl: $INSTALL_CMD"
    read -rp "Jetzt automatisch installieren? (sudo erforderlich) [j/N] " ans
    if [[ "$ans" =~ ^[jJyY]$ ]]; then
        $INSTALL_CMD
    else
        echo "Bitte Abhängigkeiten manuell installieren und erneut ausführen."
        exit 1
    fi
fi

# --- Build ---
echo ""
echo "Kompiliere …"
cd "$SCRIPT_DIR"
make clean 2>/dev/null || true
make
echo "[✓] Build erfolgreich"

# --- Binary installieren ---
mkdir -p "$HOME/.local/bin"
cp -f "$SCRIPT_DIR/$BIN_NAME" "$INSTALL_BIN"
chmod +x "$INSTALL_BIN"
echo "[✓] Binary installiert: $INSTALL_BIN"

# Sicherstellen dass ~/.local/bin im PATH ist
if ! echo "$PATH" | tr ':' '\n' | grep -qx "$HOME/.local/bin"; then
    echo ""
    echo "[!] $HOME/.local/bin ist nicht in \$PATH."
    echo "    Füge folgende Zeile in ~/.bashrc oder ~/.profile ein:"
    echo '    export PATH="$HOME/.local/bin:$PATH"'
fi

# --- input-Gruppe prüfen ---
if ! groups | grep -q '\binput\b'; then
    echo ""
    echo "[!] Dein Benutzer ist nicht in der 'input'-Gruppe."
    echo "    Ohne diese Gruppe kann das Programm das Tablet nicht lesen."
    echo ""
    echo "      sudo usermod -aG input \$USER"
    echo ""
    read -rp "Jetzt automatisch hinzufügen? (sudo erforderlich) [j/N] " ans
    if [[ "$ans" =~ ^[jJyY]$ ]]; then
        sudo usermod -aG input "$USER"
        echo "    Hinzugefügt. Bitte nach der Installation neu einloggen."
    fi
fi

# --- .desktop-Datei (Anwendungsmenü) ---
mkdir -p "$HOME/.local/share/applications"

cat > "$DESKTOP_FILE" << EOF
[Desktop Entry]
Type=Application
Name=Wacom Cursor
GenericName=Wacom Pen Cursor Overlay
Comment=Zeigt einen sichtbaren Cursor an der Position des Wacom-Stifts
Exec=${INSTALL_BIN}
Icon=input-tablet
Terminal=false
Categories=Utility;
Keywords=wacom;tablet;cursor;pen;
StartupNotify=false
EOF

echo "[✓] Desktop-Eintrag: $DESKTOP_FILE"

# --- Autostart (optional) ---
echo ""
read -rp "Soll Wacom Cursor automatisch beim Login starten? [j/N] " autostart
if [[ "$autostart" =~ ^[jJyY]$ ]]; then
    mkdir -p "$HOME/.config/autostart"
    cp -f "$DESKTOP_FILE" "$AUTOSTART_FILE"
    echo "[✓] Autostart aktiviert: $AUTOSTART_FILE"
else
    # Falls alter Autostart existiert, entfernen
    rm -f "$AUTOSTART_FILE" 2>/dev/null
fi

# --- Alten systemd-Service entfernen ---
OLD_SERVICE="$HOME/.config/systemd/user/${BIN_NAME}.service"
if [ -f "$OLD_SERVICE" ]; then
    echo ""
    echo "Alter systemd-Service gefunden, wird deaktiviert …"
    systemctl --user stop "${BIN_NAME}.service" 2>/dev/null || true
    systemctl --user disable "${BIN_NAME}.service" 2>/dev/null || true
    rm -f "$OLD_SERVICE"
    systemctl --user daemon-reload 2>/dev/null || true
    echo "[✓] Alter Service entfernt"
fi

echo ""
echo "=== Installation abgeschlossen ==="
echo ""
echo "Starten:  wacom-cursor"
echo "          oder über das GNOME-Anwendungsmenü (\"Wacom Cursor\")"
echo ""
echo "Beenden:  Rechtsklick auf das Tray-Icon → Beenden"
echo "          oder Ctrl+C im Terminal"
