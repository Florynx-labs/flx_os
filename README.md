# FLX OS

> **Plus qu'un système. Une vision.**
> Un système d'exploitation open-source, rapide et sécurisé par **Florynx Labs**.

[![Build](https://img.shields.io/badge/build-passing-brightgreen)](#) [![Arch](https://img.shields.io/badge/arch-x86__64%20%7C%20arm64-blue)](#) [![License](https://img.shields.io/badge/license-MIT-orange)](#)

---

## Présentation

FLX OS est un système d'exploitation expérimental construit **de zéro** au-dessus du kernel Linux.
Le compositeur, le gestionnaire de fenêtres, la bibliothèque graphique, le shell et la plupart des
applications sont écrits en C. File Explorer et Task Manager sont désormais des applications Rust
natives et réutilisent l'ABI C existante de libflxui. Les applications Rust n'ajoutent aucune crate tierce;
les exécutables restent liés statiquement à musl (pas de glibc, Gtk, Qt ni Electron).

L'interface est rendue pixel par pixel par notre propre rasteriseur, basé sur le travail de
Lumen OS 2 (MIT, Jakub Dostál).

```
┌─────────────────────────────────────────────┐
│  FLX OS  (0.1.0)                            │
│  ┌──────────────────────────────────────┐   │
│  │  Compositeur + WM        (src/comp/) │   │
│  │  Shell + Start menu      (src/sh/)   │   │
│  │  Apps C/Rust       (src/apps/, src/rust/) │   │
│  │  libflxui                (src/lib/)  │   │
│  │  Pile réseau + TLS       (src/net/)  │   │
│  └──────────────────────────────────────┘   │
│         Kernel Linux 6.12 (musl)            │
└─────────────────────────────────────────────┘
```

---

## Architecture

| Composant | Chemin | Description |
|-----------|--------|-------------|
| **init** | `src/init/` | PID 1 : démarre le réseau, le compositeur et le shell |
| **Compositeur / WM** | `src/comp/` | Rendu DRM/KMS, gestion des fenêtres, shell graphique |
| **libflxui** | `src/lib/` | Rasteriseur, texte, thèmes, icônes, protocole IPC |
| **Shell FLX** | `src/sh/` | Shell interactif (`flxsh`) avec prompt couleur et historique |
| **Applications** | `src/apps/` | Explorateur, Éditeur, Calc, Navigateur, Gestionnaire de tâches… |
| **Pile réseau** | `src/net/` | TCP/IP, HTTP/HTTPS, DNS — tout en stdlib + BearSSL |
| **netd** | `src/netd/` | Démon réseau DHCP/WiFi (isolation de privilèges) |
| **Bridge IA** | `bridge/` | Proxy HTTP vers Claude (accessible via 10.0.2.2:8766) |

---

## Prérequis

| Outil | Version |
|-------|---------|
| Docker | ≥ 20.10 |
| QEMU | ≥ 8.0 (`qemu-system-x86_64` ou `qemu-system-aarch64`) |
| Python | ≥ 3.9 (tests + scripts uniquement) |
| KVM | optionnel (recommandé pour x86_64 natif) |

```sh
# Ubuntu / Debian
sudo apt install -y docker.io qemu-system-x86 qemu-system-arm qemu-kvm python3
```

---

## Build & Lancement

```sh
# Cloner le dépôt
git clone https://github.com/Florynx-labs/flx-os.git
cd flx-os

# Build pour l'architecture courante (x86_64 ou arm64 auto-détecté)
./build.sh

# Lancer
./run.sh

# Forcer une architecture spécifique
FLX_ARCH=x86_64 ./build.sh
FLX_ARCH=x86_64 ./run.sh

# Affichage VNC (headless)
FLX_DISPLAY=vnc ./run.sh
```

### Variables d'environnement

| Variable | Valeur | Description |
|----------|--------|-------------|
| `FLX_ARCH` | `x86_64` \| `arm64` | Architecture cible (auto-détecté) |
| `FLX_DISPLAY` | `gtk` \| `cocoa` \| `sdl` \| `vnc` \| `none` | Mode d'affichage |
| `FLX_DISK` | chemin | Disque persistant `/home` personnalisé |
| `FLX_NO_BRIDGE` | `1` | Désactive le bridge IA |
| `FLX_QMP` | chemin socket | Socket QMP pour les tests |

---

## Architectures supportées

| Architecture | Machine QEMU | Accélération | Kernel |
|---|---|---|---|
| `x86_64` | `q35` | KVM (Linux natif), HVF (macOS), TCG | `bzImage` |
| `arm64` | `virt` | KVM (ARM Linux), HVF (Apple Silicon), TCG | `Image` |

---

## Assets & Interface

Les fonds d'écran et icônes proviennent du dossier `assets/` et sont convertis
automatiquement au build par `scripts/prepare_assets.py` :

- **Fonds d'écran** → `build/wallpapers/*.raw` (format FLXW, 1920×1080 ARGB32)
- **Icônes** → `build/icons/*.png` (128×128 RGBA)

Les noms de wallpapers attendus par le compositeur : `bloom`, `aurora`, `dune`, `lock`.
Si un fichier `.raw` est manquant, `gen_wallpaper` génère un dégradé procédural de secours.

---

## Tests

```sh
# Test bout-en-bout (headless, tous les apps)
./tests/e2e.sh

# Vérification rapide : aucune dépendance toolkit
scripts/verify_no_toolkits.sh build/
```

---

## Roadmap

- [ ] **FLX Kernel** — migration du kernel Linux vers un microkernel custom (voir [`docs/FLX_KERNEL_MIGRATION.md`](docs/FLX_KERNEL_MIGRATION.md))
- [ ] **Wayland-over-flx** — exposition d'un protocole Wayland minimal
- [ ] **Package manager** — gestionnaire de paquets natif FLX
- [ ] **GPU acceleration** — rendu OpenGL ES via virgl
- [ ] **UEFI boot** — bootloader FLX sans QEMU `-kernel`

---

## Structure du dépôt

```
flx-os/
├── src/          # Code source de l'OS (C et Rust, musl statique)
│   ├── comp/     # Compositeur + WM + shell graphique
│   ├── lib/      # libflxui : rasteriseur, texte, thème, icons, IPC
│   ├── sh/       # Shell FLX (flxsh)
│   ├── apps/     # Applications (9 apps)
│   ├── net/      # Pile réseau TCP/HTTP/HTTPS/DNS
│   └── netd/     # Démon réseau
├── kernel/       # Fragments de configuration kernel
│   ├── flx-arm64.config
│   └── flx-x86_64.config
├── docker/       # Toolchain Alpine musl
├── assets/       # Fonds d'écran, icônes, logos originaux
├── bridge/       # Bridge IA (HTTP → Claude)
├── tests/        # Tests E2E via QMP
├── tools/        # Outils de test (qmp.py)
├── scripts/      # Scripts de build et vérification
├── docs/         # Documentation technique approfondie
├── home-skel/    # Squelette /home initial
└── third_party/  # BearSSL, polices de caractères, CA bundle Mozilla
```

---

## Licence

MIT — Florynx Labs. Inspiré par [Lumen OS 2](https://github.com/nicowillis/lumen-os-2) (MIT, Jakub Dostál).
