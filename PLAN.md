# FLX OS — Plan d'architecture

> Par Florynx Labs. Open-source, sans affiliation avec aucune entreprise.
> Kernel Linux en dessous ; tout ce qui est au-dessus de `/dev/dri` et `/dev/input`
> a été écrit dans ce dépôt (C + musl statique).

## Architecture

```
 apps (processus séparés, libflxui)           flxsh (shell interactif, pty)
 explorer settings terminal notepad calc browser taskmgr minesweeper proof
         |  SOCK_SEQPACKET /run/flx.sock  +  memfd ARGB double buffers (release explicite)
 flx-compositor  — DRM/KMS + page flip, evdev, WM, shell, glass, assistant IA
 flx-netd        — client DHCP (AF_PACKET) + rtnetlink → /run/net.conf
 /sbin/init      — PID 1 : mounts, ext2 /home sur virtio-blk, respawn, shutdown/restart
 ----------------------------------------------------------------------------------
 Linux 6.12 (x86_64 / arm64, config minimale, no fbcon)    QEMU q35|virt + kvm|hvf
```

- **libflxui** : gfx (SDF rounded rects, ombres, glass blur, polygones), texte TrueType
  via stb_truetype, icônes dessinées en code, widgets immediate-mode, parseur JSON, protocole client.
- **net** : DNS, HTTP/1.1 (+chunked, redirects, keep-alive), TLS via BearSSL, inflate (de Lumen OS 2).
- **web** : tokenizer/tree builder HTML, cascade CSS, layout bloc/inline/table (de Lumen OS 2).
- **Assistant IA** : compositeur → HTTP POST 10.0.2.2:8766/ask → `bridge/copilot_bridge` (hôte) →
  `claude -p` sans outils, réponse streamée en NDJSON.

## Jalons accomplis

- [x] M0 dépôt, toolchain, kernel, init sur port série
- [x] M1 DRM 1920×1080, animation de boot, input, curseur, harness QMP, premier screenshot
- [x] M2 compositeur, protocole fenêtre, WM, libflxui, écran de verrouillage
- [x] M3 barre des tâches, barre du haut, Start, glass, thèmes
- [x] M4 Terminal + shell, Éditeur, Calculatrice, Explorateur, Paramètres/À propos
- [x] M5 pile réseau, Navigateur, Gestionnaire de tâches, infos réseau
- [x] M6 Assistant IA + bridge + fallback hors ligne
- [x] M7 Widgets, snap layouts, Alt+Tab, Minesweeper, polish
- [x] M8 mode proof, scan toolkit, e2e, README
- [x] M9 **Multi-arch x86_64 + arm64**, rebrand complet FLX OS, vrais assets visuels

## Futures évolutions

- [ ] FLX Kernel (microkernel custom) — voir `docs/FLX_KERNEL_MIGRATION.md`
- [ ] Protocole Wayland-flx minimal
- [ ] Gestionnaire de paquets natif FLX
- [ ] Accélération GPU via virgl (OpenGL ES)
- [ ] Boot UEFI sans `-kernel` QEMU
- [ ] flxsh : complétion de commandes, piping amélioré
