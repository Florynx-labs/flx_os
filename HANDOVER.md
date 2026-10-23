# FLX OS — HANDOVER

> Document de passation destiné à tout développeur reprenant le projet.
> Dernière mise à jour : octobre 2026 — Florynx Labs.

---

## 1. Vue d'ensemble

FLX OS est un système d'exploitation expérimental écrit de zéro en C pur, sans bibliothèques
externes (glibc, libstdc++, GTK, Qt...). Il tourne au-dessus d'un kernel Linux 6.12 compilé avec
une configuration minimale pour les architectures **x86_64** et **arm64**.

L'objectif à long terme est de remplacer Linux par un microkernel propriétaire (voir `docs/FLX_KERNEL_MIGRATION.md`).

---

## 2. Flux de build

```
build.sh
  └─▶ docker run --platform $PLATFORM flx-toolchain-{arch}
          └─▶ docker/build-inside.sh
                ├─ linux-6.12 (kernel/flx-{arch}.config)
                ├─ musl statique — src/src/Makefile
                ├─ scripts/prepare_assets.py  (wallpapers + icons)
                ├─ gen_wallpaper (fallback procédural)
                └─ Outputs: build/bzImage | build/Image
                            build/initramfs.cpio
                            build/home.img
run.sh
  └─▶ qemu-system-{arch}
        ├─ -kernel build/{bz}Image
        ├─ -initrd build/initramfs.cpio
        ├─ -drive  data/disk.img   (ext2 /home persistant)
        └─ -qmp    /tmp/flx-qmp.sock
```

---

## 3. Architecture du code

### 3.1 IPC — protocole FLX

Toutes les applications se connectent via **Unix socket** `/run/flx.sock`.
La constante `W12_SOCK` (alias `FLX_SOCK`) dans `src/lib/w12.h` définit ce chemin.

Le protocole est défini dans `src/lib/w12.h` (énumérations `Msg`, `MsgType`) et documenté
dans `src/comp/wm.c`.

### 3.2 Rendu graphique

- **Backend** : DRM/KMS via `virtio-gpu-pci` — le compositeur ouvre `/dev/dri/card0`.
- **Pipeline** : le compositeur (`src/comp/wm.c`) maintient un framebuffer 1920×1080 ARGB32.
  Le rendu est multi-threadé (bandes horizontales, 1 thread/cœur).
- **Rasteriseur** : `src/lib/gfx.c` — cercles, rectangles arrondis, arcs, dégradés, flou.
- **Texte** : `src/lib/font.c` — rendu TrueType à la volée (polices TTF dans `/share/fonts/`).

### 3.3 Thème & Assets

- Les wallpapers sont au format **FLXW** : magic `0x574C5846` + width + height + pixels ARGB32.
- Le compositeur charge `bloom` (index 0), `aurora` (1), `dune` (2) depuis `/share/wallpapers/`.
- Les icônes d'application PNG 128×128 sont dans `/share/icons/`.
- `scripts/prepare_assets.py` convertit les sources de `assets/` en ces formats.
- Si un wallpaper manque, `tools/gen_wallpaper.c` génère un dégradé de secours.

### 3.4 Réseau

- **Init** (`src/init/`) configure l'interface eth0 via DHCP en userland.
- **netd** (`src/netd/`) est le démon réseau avec isolation de privilèges.
- **Pile TCP/IP** (`src/net/`) : implémentation bare-metal + HTTP/HTTPS via BearSSL.
- **CA Bundle** : `third_party/cacert.pem` → `build/gen/ta.c` (symboles `flx_tas`, `flx_tas_num`).

### 3.5 Shell

- `src/sh/w12sh.c` est le shell interactif (`flxsh`) : prompt couleur, historique, `fetch`, `proof`, `neofetch`.
- La commande `fetch` affiche les infos système avec le logo Florynx ASCII.

---

## 4. Multi-architecture

| Paramètre | x86_64 | arm64 |
|-----------|--------|-------|
| `FLX_ARCH` | `x86_64` | `arm64` |
| Machine QEMU | `q35` | `virt` |
| Kernel image | `build/bzImage` | `build/Image` |
| Console | `ttyS0` | `ttyAMA0` |
| Config kernel | `kernel/flx-x86_64.config` | `kernel/flx-arm64.config` |
| Volume Docker | `flx-kbuild-x86_64` | `flx-kbuild-arm64` |

---

## 5. Tests

Le test E2E (`tests/e2e.py`) :
1. Boot headless (`FLX_DISPLAY=none`)
2. Attend les marqueurs série `W12:READY`, `W12:BOOT`, etc.
3. Pilote les applications via QMP (clics absolus tablet + clavier)
4. Prend des screenshots à chaque étape dans `tests/out/`
5. Vérifie les marqueurs série attendus

```sh
./tests/e2e.sh              # lance tous les tests
python3 tests/e2e.py -v     # verbose
```

---

## 6. Fichiers critiques

| Fichier | Rôle |
|---------|------|
| `src/lib/w12.h` | En-tête global partagé — constantes, types, API libflxui |
| `src/comp/shell.c` | Shell graphique : start menu, barre des tâches, lock screen, wallpapers |
| `src/comp/wm.c` | WM : fenêtres, focus, resize, snap layouts, animations |
| `src/lib/icons.c` | Icônes d'apps et glyphes dessinés en code, table APPS[] |
| `docker/build-inside.sh` | Script de build complet dans le conteneur |
| `kernel/flx-x86_64.config` | Fragment config kernel x86_64 |
| `kernel/flx-arm64.config` | Fragment config kernel arm64 |

---

## 7. Points d'attention

> [!CAUTION]
> Le socket IPC `/run/flx.sock` a été renommé de `/run/w12.sock` — vérifier que
> tous les anciens tests ou scripts custom utilisent le nouveau chemin (ou `W12_SOCK`/`FLX_SOCK`).

> [!WARNING]
> Les symboles TLS (`w12_tas`) ont été renommés en `flx_tas` — forcer un rebuild complet
> si vous avez un volume Docker kbuild existant (`docker volume rm flx-kbuild-arm64`).

> [!NOTE]
> La variable d'environnement `W12_QMP` est encore acceptée comme alias de `FLX_QMP` dans
> `tools/qmp.py` pour rétrocompatibilité.

---

## 8. Prochaines étapes

- [ ] Intégrer les vrais wallpapers HD dans `assets/wallpapers/`
- [ ] Charger les icônes PNG au runtime depuis `/share/icons/` (voir `src/lib/icons.c:icon_app`)
- [ ] Exposer un protocole Wayland minimal
- [ ] Migrer vers un kernel custom (voir `docs/FLX_KERNEL_MIGRATION.md`)
- [ ] Signature cryptographique des binaires dans l'initramfs
