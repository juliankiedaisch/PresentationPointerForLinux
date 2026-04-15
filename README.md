# Wacom Cursor Overlay (PresentationPointerForLinux)

Ein transparentes, klick-durchlässiges Fadenkreuz-Overlay für den Wacom-Stift unter GNOME/Wayland. Das Programm zeigt einen sichtbaren Cursor an der aktuellen Stiftposition an – ideal für Präsentationen mit einem Wacom-Tablet.

Da GNOME/Mutter kein `wlr-layer-shell` unterstützt, wird XWayland als Rendering-Backend verwendet.

## Features

- Fadenkreuz-Overlay folgt dem Wacom-Stift in Echtzeit
- Fenster ist vollständig klick-durchlässig (Eingaben werden durchgereicht)
- Automatische Erkennung des Wacom-Geräts via `/proc/bus/input/devices`
- Systemtray-Icon (AppIndicator) zum Wechseln des Monitors und Beenden
- Multi-Monitor-Unterstützung via XRandR
- Konfigurierbarer Cursor-Radius

## Abhängigkeiten

| Paket | Debian/Ubuntu | Fedora/RHEL | Arch Linux |
|---|---|---|---|
| libx11 | `libx11-dev` | `libX11-devel` | `libx11` |
| libxfixes | `libxfixes-dev` | `libXfixes-devel` | `libxfixes` |
| libxext | `libxext-dev` | `libXext-devel` | `libxext` |
| libxrandr | `libxrandr-dev` | `libXrandr-devel` | `libxrandr` |
| cairo | `libcairo2-dev` | `cairo-devel` | `cairo` |
| libevdev | `libevdev-dev` | `libevdev-devel` | `libevdev` |
| gtk3 | `libgtk-3-dev` | `gtk3-devel` | `gtk3` |
| libappindicator | `libappindicator3-dev` | `libappindicator-gtk3-devel` | `libappindicator-gtk3` |

Außerdem werden `gcc`, `make` und `pkg-config` benötigt.

## Installation

Das mitgelieferte Installationsskript prüft Abhängigkeiten, kompiliert das Programm und richtet optional einen Autostart-Eintrag ein:

```bash
bash install.sh
```

Das Skript:
1. Prüft alle Build-Abhängigkeiten und bietet automatische Installation an
2. Kompiliert das Programm mit `make`
3. Installiert das Binary nach `~/.local/bin/wacom-cursor`
4. Erstellt einen `.desktop`-Eintrag im Anwendungsmenü
5. Richtet optional einen Autostart-Eintrag ein

### Manuelle Installation

```bash
make
cp wacom-cursor ~/.local/bin/
```

## Berechtigungen

Das Programm liest das Eingabegerät direkt via `libevdev`. Dafür muss der Benutzer in der `input`-Gruppe sein:

```bash
sudo usermod -aG input $USER
# Danach neu einloggen
```

## Verwendung

```bash
wacom-cursor
```

Beenden mit `Ctrl+C` oder über das Systemtray-Icon.

### Kommandozeilenoptionen

| Option | Beschreibung |
|---|---|
| `--monitor NAME` | Cursor auf diesem Monitor anzeigen (z. B. `HDMI-1`) |
| `--radius N` | Fadenkreuz-Radius in Pixel (Standard: `8`) |
| `--device /dev/input/eventX` | Wacom-Gerät manuell angeben |

## Build

```bash
make          # Kompilieren
make clean    # Build-Artefakte entfernen
```

## Lizenz

GPL-3.0 – siehe [LICENSE](LICENSE).
