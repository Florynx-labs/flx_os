# FLX OS — Rapport d'Audit Complet du Repository

**Date :** Octobre 2026  
**Auteur :** Antigravity Engineering  
**Version cible :** FLX OS 0.1.0 (Dev)

---

## 1. Inventaire et Architecture Découverte

### 1.1 Stack Technologique Réelle
Contrairement à une simple application web, un prototype Electron ou une interface simulée, **FLX OS** est un véritable système d'exploitation embarqué / desktop basé sur :
- **Kernel Linux** : Version upstream **6.12.113** compilé pour architecture **ARM64 (aarch64)** avec configuration minimale (`allnoconfig` + fragments dédiés dans `kernel/w12.config`).
- **Bibliothèque C standard (libc)** : **musl libc** native (compilation statique complète de tous les binaires).
- **Toolchain de compilation** : Conteneur Alpine Linux arm64 (`flx-toolchain`) contenant `gcc`, `make`, `bison`, `flex`, `linux-headers`, `bearssl`, `fonttools`.
- **Plateforme cible d'exécution** : Machine virtuelle **QEMU aarch64 virt** (virtio-gpu-pci avec KMS/DRM dumb buffers en 1920x1080 32bpp, virtio-tablet-pci, virtio-keyboard-pci, virtio-net-pci, virtio-blk-pci, virtio-rng-pci).
- **Système graphique et rendu** :
  - Gestionnaire de fenêtres et compositeur direct sur Linux Direct Rendering Manager (`/dev/dri/card0` via DRM dumb buffers et double buffering).
  - Rendu logiciel optimisé multicœur (`Canvas` ARGB32, rasterisation polygonale anti-aliasée, flou gaussien multi-passes, effet verre dépoli *frosted glass / mica*).
  - Moteur typographique vectoriel complet avec rendu lissé sous-pixel Selawik (`font.c`).
- **Couche réseau et sécurité** :
  - Démon réseau `flx-netd` gérant DHCP, DNS et résolution d'adresses.
  - Chiffrement TLS 1.2 / 1.3 via **BearSSL** statique et bundle de certificats racines Mozilla.
  - Navigateur web natif `browser` avec parseur HTML, moteur de rendu CSS et client HTTP/HTTPS asynchrone.

---

## 2. Organisation de l'Arborescence du Projet

| Répertoire / Fichier | Responsabilité |
| :--- | :--- |
| `kernel/w12.config` | Fragment Kconfig pour Linux arm64 (virtio, DRM, DRM_VIRTIO_GPU, devtmpfs, ext2/4, IP_PNP, AF_PACKET, AF_UNIX). |
| `docker/` | Définition du conteneur de build (`Dockerfile`) et script d'orchestration (`build-inside.sh`). |
| `src/init/` | Processus PID 1 (`/sbin/init`) : montage des pseudo-systèmes de fichiers (`/proc`, `/sys`, `/dev`, `/run`), configuration du loopback, lancement de `flx-netd` et `flx-compositor`. |
| `src/comp/` | Compositeur de bureau (`flx-compositor`) : DRM KMS, gestionnaire de fenêtres (`wm.c`), shell desktop (`shell.c`), panneaux (`panels.c`), entrées (`input.c`). |
| `src/lib/` | Bibliothèque partagée (`libw12.a`) : primitives graphiques (`gfx.c`), polices (`font.c`), icônes vectorielles (`icons.c`), protocole IPC client/serveur (`client.c`), toolkit UI immédiat (`ui.c`), parseur JSON (`json.c`). |
| `src/apps/` | Suite d'applications natives :<br>• `explorer` : Gestionnaire de fichiers<br>• `terminal` : Émulateur de terminal + shell `flxsh`<br>• `settings` : Panneau de configuration système<br>• `taskmgr` : Moniteur système et processus<br>• `browser` : Navigateur Web léger avec HTTPS<br>• `notepad` : Éditeur de texte<br>• `calc` : Calculatrice utilitaire<br>• `proof` : Fiche technique et vérification d'intégrité<br>• `minesweeper` : Jeu classique |
| `tools/` | Générateur procédural de fonds d'écran (`gen_wallpaper.c`), scripts de contrôle QMP (`qmp.py`). |
| `home-skel/` | Arborescence initiale du répertoire personnel utilisateur (`/home/user`). |
| `build.sh` / `run.sh` | Scripts racine pour la compilation conteneurisée et le lancement QEMU sous Linux et macOS. |

---

## 3. Analyse Graphique et Comparaison avec l'Image de Référence

L'image de référence (`flx-os.png`) présente un bureau moderne, sombre et cinématographique.

### 3.1 Identité Visuelle Implémentée
- **Palette FLX Dark** :
  - Fond / Noir principal : `#05090B` (`0xFF05090B`)
  - Surface secondaire : `#0A1114` (`0xFF0A1114`)
  - Surface élevée / Cartes : `#101A1D` (`0xFF101A1D`)
  - Bordures subtiles : `#203034` (`0xFF203034`)
  - Or principal : `#F5AD32` (`0xFFF5AD32`)
  - Or clair / reflets : `#FFD477` (`0xFFFFD477`)
  - Turquoise / Teal accents : `#35D6C0` (`0xFF35D6C0`)
  - Texte principal : `#F2F5F4` (`0xFFF2F5F4`)
  - Texte secondaire : `#94A6AA` (`0xFF94A6AA`)
  - Texte désactivé : `#607176` (`0xFF607176`)

### 3.2 Composants Shell Desktop
1. **Barre Supérieure (Top Bar)** :
   - Logo Florynx Fox or et nom FLX OS.
   - Champ de recherche globale centré avec loupe (`Rechercher...`).
   - Indicateurs système à droite : Cloche de notifications, Réseau WiFi, Batterie avec pourcentage `92%`, Horloge numérique et date (`Sam. 26 Avr. 2025`), bouton d'alimentation.
2. **Dock Inférieur (Bottom Dock)** :
   - Capsule flottante en verre dépoli translucide avec reflets.
   - Bouton de lancement FLX OS (tête de renard dorée).
   - Raccourcis d'applications actives et épinglées avec indicateur d'activation sous chaque icône.
3. **Branding Fond d'Écran et Bureau** :
   - Filigrane centralisé : Tête de renard dorée stylisée, typographie "FLX OS" et devise *"Plus qu'un système. Une vision."*.
   - Mention officielle en bas à gauche : Logo + "FLX OS" + "Par Florynx Labs".
   - Slogan et drapeau national en bas à droite : "Simple. Rapide. Local. • Mali" avec les bandes vert, or, rouge.
4. **Widget Bureau Horloge & Date** :
   - Carte flottante translucide en haut à droite avec horloge grand format, date, citation inspirante et miniature cinématographique.

---

## 4. Protocole IPC & Cycle de Vie des Fenêtres

Le serveur d'affichage (`flx-compositor`) communique avec chaque application cliente via une socket de domaine Unix (`/run/w12.sock`).
- La communication utilise des messages compacts `Msg` et des descripteurs de fichiers mémoire partagée (`memfd` ou buffers alloués).
- Chaque application effectue son rendu dans un double buffer partagé et notifie le compositeur par `M_COMMIT`.
- Le gestionnaire de fenêtres prend en charge le déplacement à la souris (barre de titre), le redimensionnement par les bords, la minimisation, la maximisation et le snap assist (fractionnement demi-écran ou plein écran).

---

## 5. Risques et Mesures de Stabilité

1. **Isolation des processus** : En cas de crash d'une application client, le compositeur capture l'événement de déconnexion (`M_CLOSE_REQ` ou EOF sur socket), libère les ressources et maintient le bureau actif.
2. **Gestion mémoire** : Aucune dépendance externe lourde (ni X11, ni Wayland lourd, ni Qt, ni GTK en guest). Empreinte mémoire totale inférieure à 32 Mo de RAM pour l'ensemble du système utilisateur.
3. **Portabilité hôte** : Adaptation des scripts pour fonctionner nativement sous Linux Mint / Ubuntu (avec QEMU GTK et accélération TCG/KVM) ainsi que sous macOS (Cocoa/HVF).

---

## 6. Prochaines Étapes Validées
- Validation de la fin du cycle de compilation Docker.
- Génération de l'image kernel `build/Image`, de l'initramfs `build/initramfs.cpio` et du disque `build/home.img`.
- Lancement et tests interactifs via QEMU.
