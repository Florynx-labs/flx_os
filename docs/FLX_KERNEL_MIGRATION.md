# FLX Kernel — Plan de Migration Linux → Kernel Custom

> **Florynx Labs — Document de planification stratégique**
> Statut : *Draft v0.1* — Octobre 2026
> Auteur : Équipe noyau Florynx

---

## 1. Contexte et motivation

FLX OS tourne actuellement sur un **Linux 6.12** configuré de façon minimale.
Cette approche a permis d'itérer rapidement sur les couches userland, mais Linux
apporte avec lui une complexité structurelle qui s'oppose à plusieurs objectifs de FLX OS :

| Objectif | Problème avec Linux |
|----------|---------------------|
| Sécurité par isolation forte | Monolithique : une faille kernel = compromis total |
| Démarrage < 200 ms | Linux init + modules même minimaux = ~400 ms |
| Surface d'attaque minimale | Millions de lignes de code dans le kernel ring0 |
| Code 100 % auditable | Impossible avec Linux (trop grand) |
| Portabilité extrême | Abstraction POSIX inadaptée à notre modèle d'IPC |

Le **FLX Kernel** (`flxk`) sera un **microkernel exokernel hybride** : seuls les mécanismes
indispensables (scheduling, mémoire physique, communication inter-processus, drivers minimaux)
s'exécuteront en ring 0. Tout le reste tourne en ring 3.

---

## 2. Architecture cible du FLX Kernel

```
Ring 0 (FLX Kernel — flxk)
├── Scheduler             préemptif, O(1), multi-cœur
├── PMM / VMM             pages physiques + espace d'adressage virtuel par tâche
├── IPC primitif          canaux de messages typés à latence garantie
├── Interruptions/IRQ     table vectorisée, affinités par CPU
└── Drivers minimum       UART, LAPIC/IOAPIC, framebuffer (VESA/UEFI GOP)

Ring 3 (Servers — processus isolés)
├── flxvfs    — serveur VFS (file systems)
├── flxnet    — pile réseau (reprend src/net/ de l'OS actuel)
├── flxdrm    — pilote GPU DRM/KMS (userspace driver via /dev/dri)
├── flxinit   — gestionnaire de services (remplace src/init/)
└── flxsec    — serveur de sécurité (capabilities, ACL)

Userland (inchangé)
└── Compositeur, WM, Applications, Shell (src/)
```

---

## 3. Phases de migration

### Phase 0 — Préparation (3-6 mois) ✅ En cours

- [x] Extraire tout le code userland de Linux (déjà fait : tout tourne via IPC socket)
- [x] Documenter les syscalls Linux utilisés par FLX OS
- [ ] Créer un shim POSIX (`libflxposix`) qui intercepte les syscalls → IPC flxk
- [ ] Écrire la suite de tests syscall (`tests/syscall_compat.py`)

**Syscalls Linux actuellement utilisés par FLX OS :**

| Catégorie | Syscalls |
|-----------|---------|
| Mémoire | `mmap`, `munmap`, `mprotect`, `memfd_create` |
| Processus | `fork`, `execve`, `waitpid`, `exit`, `prctl` |
| IPC | `socket(AF_UNIX)`, `bind`, `connect`, `sendmsg`, `recvmsg` |
| Fichiers | `open`, `read`, `write`, `close`, `stat`, `mkdir`, `unlink` |
| Temps | `clock_gettime`, `nanosleep` |
| Réseau | `socket(AF_INET)`, `connect`, `send`, `recv`, `poll` |
| Signal | `sigaction`, `rt_sigprocmask` |
| Divers | `ioctl` (DRM/KMS, tty), `epoll`, `timerfd` |

### Phase 1 — flxk Bootloader et HAL (6-12 mois)

Objectif : booter un noyau custom bare-metal sur QEMU q35 et `virt`.

- [ ] **Bootloader UEFI** (`boot/`) : charge flxk depuis la partition EFI
- [ ] **HAL x86_64** : GDT/IDT, LAPIC, IOAPIC, ACPI parse, SMP init
- [ ] **HAL arm64** : exception vectors, GIC v3, PSCI
- [ ] **PMM** : allocateur de pages physiques (buddy system)
- [ ] **VMM** : espace d'adressage virtuel, page fault handler, CoW
- [ ] **UART driver** : output série dès le début pour debug
- [ ] **Premier userspace** : `flxinit` minimal qui affiche "FLX Kernel booted"

### Phase 2 — IPC et Servers (6-12 mois)

Objectif : les processus ring 3 s'exécutent et communiquent de façon fiable.

- [ ] **IPC flxk** : canaux de messages zero-copy (shared memory + notification)
- [ ] **Scheduler SMP** : work stealing, affinités NUMA
- [ ] **flxvfs** : VFS userspace avec drivers ext2/tmpfs en ring 3
- [ ] **flxnet** : pile réseau portée depuis `src/net/`
- [ ] **Capabilities** : chaque processus déclare les ressources qu'il peut demander

### Phase 3 — GPU et Compositeur (6-12 mois)

Objectif : l'interface graphique de FLX OS tourne entièrement sans Linux.

- [ ] **flxdrm** : driver DRM/KMS userspace via PCIe MMIO (virtio-gpu, puis GPU réel)
- [ ] **Protocol Wayland-flx** : protocole graphique natif FLX (compatible Wayland subset)
- [ ] **Migration compositeur** : `src/comp/` adapté pour Wayland-flx au lieu de DRM direct

### Phase 4 — Sécurité avancée (ongoing)

- [ ] **Secure Boot** : vérification de signature Ed25519 au boot
- [ ] **Isolation hardware** : Intel VT-x / ARM TrustZone pour les clés cryptographiques
- [ ] **Memory tagging** : ARM MTE pour détecter les corruptions mémoire ring 3
- [ ] **CFI** : Control Flow Integrity compilé dans tous les binaires

---

## 4. Interfaces de compatibilité

Pour permettre une migration progressive sans réécrire l'OS d'un coup :

```
┌─────────────────────────────────────┐
│  Applications FLX OS (inchangées)   │
│  (syscalls POSIX via libflxposix)   │
├─────────────────────────────────────┤
│  libflxposix                        │  ← shim POSIX → flxk IPC
├─────────────────────────────────────┤
│  FLX Kernel (flxk)                  │
└─────────────────────────────────────┘
```

`libflxposix` traduit les appels POSIX standards en messages IPC flxk.
Les applications n'ont pas besoin d'être modifiées dans un premier temps.

---

## 5. Stack technologique

| Composant | Langage | Justification |
|-----------|---------|---------------|
| FLX Kernel core | C (C11) + ASM | Performance, contrôle total |
| Drivers ring 3 | C (C11) | Cohérence avec le reste du codebase |
| Bootloader | C + NASM | Compatibilité UEFI standard |
| Tests kernel | Python + shell | Réutilise l'infrastructure E2E existante |
| Documentation | Markdown | Cohérence avec le reste du projet |

**Compilateur** : `clang` avec LTO + CFI (pour le kernel), `gcc` musl pour userland.

---

## 6. Métriques de succès

| Indicateur | Cible |
|------------|-------|
| Temps de boot | < 200 ms (UEFI → shell graphique) |
| Lignes de code noyau | < 50 000 (auditable par une petite équipe) |
| Syscalls exposés | < 60 (vs ~400 pour Linux) |
| Isolation processus | 0 partage mémoire sans IPC explicite |
| CVE critiques/an | < 2 (surface d'attaque réduite) |

---

## 7. Ressources et références

| Référence | Utilité |
|-----------|---------|
| [SerenityOS](https://github.com/SerenityOS/serenity) | Microkernel userland-first en C++ |
| [Redox OS](https://www.redox-os.org/) | Microkernel Rust, bonne structure |
| [Minix 3](https://www.minix3.org/) | Architecture de référence microkernel |
| [OSDev Wiki](https://wiki.osdev.org/) | Référence technique x86/ARM bare-metal |
| [ARM Architecture Reference](https://developer.arm.com/documentation/) | AArch64 exception model, GIC |
| [Intel SDM](https://software.intel.com/content/www/us/en/develop/articles/intel-sdm.html) | x86_64 paging, APIC, VMX |
| Linux kernel `make allnoconfig` | Point de départ pour identifier les options nécessaires |

---

## 8. Risques

| Risque | Probabilité | Mitigation |
|--------|-------------|------------|
| Complexité du porting DRM | Haute | Commencer par framebuffer VESA/GOP simple |
| Régression réseau | Moyenne | Garder `src/net/` inchangé, exposer via flxnet |
| Breaking change IPC | Haute | Shim libflxposix maintenu en parallèle |
| Manque de ressources humaines | Haute | Phases 0→2 réalisables à 2-3 personnes |
| Compatibilité logicielle tierce | Basse | FLX OS est un OS from-scratch, pas de legacy |

---

## 9. Prochaine action concrète

1. **Créer `kernel-custom/`** dans le dépôt (branche `flxk-dev`)
2. **Boot QEMU q35 x86_64 bare-metal** : afficher "FLX Kernel 0.0.1" sur UART
3. **Documenter tous les syscalls** utilisés par `src/` avec `strace` dans QEMU
4. **Écrire `libflxposix` minimal** : `open`, `read`, `write`, `close`, `mmap`

> Pour toute question sur ce plan, ouvrir une issue GitHub avec le label `flxk`.
