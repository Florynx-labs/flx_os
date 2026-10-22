# FLX OS — Document d'Architecture Système

**Version :** 0.1.0 (Dev)  
**Écosystème :** Florynx Labs  
**Architecture Cible :** Linux Kernel 6.12 (ARM64) + Musl Userland + DRM/KMS Compositor

---

## 1. Vue d'Ensemble des Couches Système

FLX OS est structuré en plusieurs couches modulaires et indépendantes garantissant une performance maximale, une empreinte mémoire réduite (< 32 Mo) et une robustesse temps-réel :

```
+-------------------------------------------------------------------------+
|                        APPLICATIONS NATIVES FLX                        |
|  [Fichiers] [Terminal] [Paramètres] [Task Manager] [Navigateur] [Code]   |
+-------------------------------------------------------------------------+
                                    | (Unix Domain Socket: /run/w12.sock)
                                    v
+-------------------------------------------------------------------------+
|                        FLX COMPOSITOR & SHELL                           |
|  +-------------------------+  +---------------------------------------+  |
|  | Window Manager (wm.c)   |  | Shell Desktop (shell.c, panels.c)     |  |
|  | • Focus, Z-order, Snap  |  | • Top Bar, Bottom Dock, Start Menu    |  |
|  | • Multi-threaded bands  |  | • Widgets, Control Center, Quick Sett.|  |
|  +-------------------------+  +---------------------------------------+  |
|  +--------------------------------------------------------------------+  |
|  | libflxui (gfx.c, font.c, icons.c, ui.c, json.c)                    |  |
|  +--------------------------------------------------------------------+  |
+-------------------------------------------------------------------------+
                                    | (Linux DRM / evdev)
                                    v
+-------------------------------------------------------------------------+
|                             LINUX KERNEL                                |
|   /dev/dri/card0 (DRM Dumb Buffers 1920x1080) | /dev/input/event*       |
+-------------------------------------------------------------------------+
```

---

## 2. Le Moteur Graphique et Compositor (`flx-compositor`)

### 2.1 Pilote DRM / KMS Direct
Le compositeur prend le contrôle exclusif du périphérique d'affichage `/dev/dri/card0` sans aucun serveur intermédiaire (aucun X.Org ni Wayland générique).
- Allocation de deux *dumb buffers* DRM en résolution native 1920x1080 32bpp (ARGB8888).
- Synchronisation verticale via `drmModePageFlip` garantissant un affichage sans déchirement (*tear-free*).
- Gestion des interruptions d'affichage et mise en veille automatique lors de l'inactivité.

### 2.2 Rendu Multicœur par Bandes (`compose_band`)
Pour maintenir 60 images par seconde même sous émulation QEMU :
- L'écran est segmenté horizontalement en 4 bandes indépendantes calculées en parallèle sur 4 threads POSIX (`pthread`).
- Chaque thread gère le blit du fond d'écran, les fenêtres visibles situées dans sa région de découpage (`g_clip`), ainsi que les ombres douces et bordures lissées.
- Les effets de verre dépoli (*frosted glass* / *mica*) sont appliqués en post-traitement pour échantillonner les pixels composites réels.

---

## 3. Gestionnaire de Fenêtres (`wm.c`)

Le gestionnaire de fenêtres FLX implémente un modèle complet de fenêtrage moderne :
- **Barre de titre intégrée** (40 px) avec icône d'application, titre vectoriel lissé et boutons d'action (réduire, agrandir, fermer).
- **Raccourcis de placement (Snap Assist)** :
  - Survol du bouton d'agrandissement : ouverture du menu de disposition (moitié gauche, moitié droite, quadrants).
  - Déplacement d'une fenêtre vers un bord d'écran : affichage d'une prévisualisation translucide de la zone d'ancrage.
  - Sélection automatique de la fenêtre adjacente pour combler l'espace disponible.
- **Raccourcis Clavier Système** :
  - `Super` / `Win` : Ouverture / fermeture du lanceur d'applications.
  - `Alt + Tab` : Sélecteur d'applications actives avec miniatures en temps réel.
  - `Super + Flèches` : Ancrage immédiat de la fenêtre active.
  - `Alt + F4` : Fermeture propre de la fenêtre courante.
  - `Échap` : Fermeture de tous les panneaux volants ouverts.

---

## 4. Design System et Charte Visuelle FLX OS

Le design system est unifié dans `libw12.h` et `icons.c` :

| Token | Hexadécimal | Utilisation principale |
| :--- | :--- | :--- |
| `TH.bg` | `#05090B` | Fond d'écran sombre, base de l'environnement |
| `TH.surface` | `#0A1114` | Surfaces des fenêtres et barres d'outils |
| `TH.card` | `#101A1D` | Cartes surélevées, listes d'éléments, conteneurs |
| `TH.line` | `#203034` | Séparateurs, bordures fines 1px anti-aliasées |
| `TH.accent` | `#F5AD32` | Or principal FLX : focus, sélections, indicateurs |
| `TH.accent_soft` | `#35D6C0` | Turquoise d'accent secondaire et touches technologiques |
| `TH.text` | `#F2F5F4` | Texte principal à haut contraste |
| `TH.sub` | `#94A6AA` | Métadonnées, sous-titres, texte secondaire |
| `TH.dim` | `#607176` | Éléments inactifs, placeholders de saisie |

Toutes les icônes (dossiers, fichiers, matériel, paramètres, logos, drapeaux) sont dessinées par calcul analytique vectoriel dans le code source (`icons.c`), évitant toute dépendance à des polices d'icônes externes ou des images bitmaps bitmap pixellisées.

---

## 5. Protocole de Communication Client-Serveur

Les applications natives s'exécutent dans des processus séparés isolés et dialoguent avec `flx-compositor` :
1. **Initialisation** : L'application se connecte à `/run/w12.sock` et émet `M_HELLO` avec son PID, ses dimensions souhaitées et son titre.
2. **Buffer d'affichage** : Deux descripteurs de mémoire partagée sont alloués pour le double-buffering.
3. **Boucle d'événements** :
   - Événements souris (`M_MOUSE`) : coordonnées locales, boutons, défilement molette.
   - Événements clavier (`M_KEY`) : code touche, modificateurs (Shift, Ctrl, Alt, Super), séquence UTF-8.
   - Événements système (`M_CONFIGURE`, `M_FOCUS`, `M_CLOSE_REQ`, `M_THEME`).
4. **Validation de frame** : L'application termine son tracé et émet `M_COMMIT`.

---

## 6. Services d'Arrière-Plan

- **`init` (PID 1)** :
  - Monte `/dev`, `/dev/pts`, `/proc`, `/sys`, `/run`, `/home`.
  - Initialise l'interface réseau loopback `lo`.
  - Lance le démon réseau `flx-netd` puis le serveur d'affichage `flx-compositor`.
- **`flx-netd`** :
  - Détecte l'interface Ethernet virtuelle (`eth0`).
  - Négocie l'adresse IP via DHCP et extrait les serveurs DNS.
  - Écrit l'état réseau dans `/run/netinfo` pour la consultation instantanée par la barre supérieure et les paramètres.
