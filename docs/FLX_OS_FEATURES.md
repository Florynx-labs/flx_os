# FLX OS — État des Fonctionnalités et Applications

**Version :** 0.1.0 (Dev)  
**Système :** FLX OS / Florynx Labs  
**Environnement :** Linux 6.12 (ARM64) + DRM Compositor 1920x1080

---

## 1. Matrice des Fonctionnalités

Chaque fonctionnalité est classée conformément aux spécifications du Master Prompt :
- **Fonctionnelle** : Implémentée nativement et opérationnelle dans l'environnement.
- **Partielle** : Implémentée avec certaines limites techniques documentées.
- **Simulée** : Interface de démonstration ou de prévisualisation clairement identifiée.
- **Bloquée** : Dépendant d'une ressource ou d'un matériel non émulé.

---

### A. Shell Desktop & Environnement Visuel

| Composant | Statut | Détails et Fonctionnement |
| :--- | :--- | :--- |
| **Fond d'écran cinématique** | **Fonctionnelle** | Généré de manière procédurale par `gen_wallpaper.c` (`bloom.raw`). Coucher de soleil doré sur skyline urbaine avec vallée fluviale, exactement selon l'image de référence. |
| **Barre supérieure (Top Bar)** | **Fonctionnelle** | Verre dépoli 36 px. Affiche le logo FLX OS, le champ de recherche global, la cloche de notification, le statut WiFi, la batterie avec pourcentage, l'horloge numérique, la date et le bouton de veille. |
| **Dock inférieur (Bottom Dock)** | **Fonctionnelle** | Capsule translucide centrée avec effet de survol, rebond tactile au clic, pastille lumineuse pour les fenêtres ouvertes et mise en avant de l'application active. |
| **Lanceur d'applications (Start)** | **Fonctionnelle** | Lanceur complet avec barre de recherche instantanée, grille d'applications favorites, section recommandés, sélecteur alphabétique de toutes les applications, profil utilisateur et options d'alimentation. |
| **Widget de bureau Horloge** | **Fonctionnelle** | Carte flottante en haut à droite avec affichage grand format de l'heure, date complète, citation officielle FLX OS et miniature de paysage. |
| **Branding & Filigrane** | **Fonctionnelle** | Logo renard doré en filigrane central du bureau avec devise *"Plus qu'un système. Une vision."*, signature Florynx Labs en bas à gauche et devise nationale *"Simple. Rapide. Local. • Mali"* avec drapeau en bas à droite. |
| **Centre de contrôle rapide** | **Fonctionnelle** | Panneau coulissant activé depuis la zone réseau/son/batterie : réglage du volume, luminosité d'écran, bascule mode sombre/clair, sélection de fond d'écran. |

---

### B. Gestionnaire de Fenêtres (Window Manager)

| Fonctionnalité | Statut | Détails et Fonctionnement |
| :--- | :--- | :--- |
| **Déplacement & Redimensionnement** | **Fonctionnelle** | Déplacement fluide à la souris par la barre de titre. Redimensionnement par les bords et les coins avec curseur adapté. |
| **Snap Assist & Dispositions** | **Fonctionnelle** | Ancrage automatique par glisser-déposer vers les bords d'écran ou par survol du bouton d'agrandissement. Assistance visuelle pour combler la zone restante. |
| **Alt + Tab** | **Fonctionnelle** | Sélecteur multitâche avec prévisualisation des fenêtres et navigation au clavier. |
| **Minimisation / Maximisation** | **Fonctionnelle** | Réduction dans le dock et restauration immédiate sans perte d'état. |

---

### C. Applications Natives

| Application | Statut | Description Technique |
| :--- | :--- | :--- |
| **FLX Files (`explorer`)** | **Fonctionnelle** | Gestionnaire de fichiers réel connecté au système de fichiers Linux (`/home`, `/tmp`, etc.). Navigation arborescente, panneau latéral (Dossier personnel, Bureau, Documents, Images, Téléchargements, Périphériques, Réseau), fil d'Ariane interactif, création et renommage de dossiers, ouverture directe dans l'éditeur. |
| **FLX Terminal (`terminal` + `flxsh`)** | **Fonctionnelle** | Émulateur de terminal natif sur PTY Linux avec onglets multiples, défilement avec molette, historique des commandes, support des utilitaires POSIX (`ls`, `cat`, `ps`, `ifconfig`, `ping`, `neofetch`, etc.) et affichage de la mascotte renard en ASCII doré. |
| **FLX Settings (`settings`)** | **Partiellement fonctionnelle** | Pages Système, Affichage, Réseau, Son, Bluetooth et Périphériques. Le daemon root `flx-deviced` expose un inventaire noyau en lecture seule et des commandes strictement limitées pour ALSA volume, rfkill et rétroéclairage; les commandes ne sont actives que si l’appareil et son pilote existent. Certaines fonctions matérielles restent indisponibles dans QEMU. |
| **FLX Task Manager (`taskmgr`)** | **Fonctionnelle** | Moniteur de processus réel basé sur l'inspection de `/proc`. Affiche PID, nom du binaire, état, utilisation mémoire réelle, et permet de terminer un processus (`End task` via signal SIGTERM/SIGKILL). |
| **FLX Browser (`browser`)** | **Expérimental** | Parseur HTML/CSS natif, requêtes HTTP/HTTPS BearSSL et moteur QuickJS ES2023 embarqué. Les scripts inline et externes sont bornés en mémoire et durée; le DOM/Web API, le chargement asynchrone et la compatibilité des sites modernes restent incomplets. |
| **FLX Code (`notepad`)** | **Fonctionnelle** | Éditeur de texte complet avec coloration syntaxique C/Markdown, numérotation des lignes, recherche de texte, ouverture et sauvegarde de fichiers sur le disque persistant. |
| **FLX Calculator (`calc`)** | **Fonctionnelle** | Calculatrice arithmétique standard avec historique des calculs et saisie clavier/souris. |
| **FLX System Info (`proof`)** | **Fonctionnelle** | Tableau de bord technique certifiant l'architecture logicielle : comptage des lignes de code par composant, version du kernel, intégrité de la pile graphique sans framework tiers. |
| **FLX Store** | **Partielle** | Catalogue des applications de l'écosystème FLX OS. Lancement direct des applications intégrées ; installation de nouveaux paquets binaires préfigurée pour l'intégration de paquets logiciels futurs. |
| **Assistant Copilot** | **Partielle** | Panneau d'assistance conversationnelle communiquant avec le pont hôte `bridge/copilot_bridge`. Mode hors-ligne pris en charge avec message informatif en l'absence de clé de modèle. |

---

## 2. Intégration et Services Système

1. **Système de fichiers persistant** :
   Le disque template `build/home.img` est initialisé au format `ext2` et monté par `init` sur `/home`. Les fichiers créés sur le bureau ou dans les documents sont conservés entre les sessions dans `data/disk.img`.
2. **Configuration réseau dynamique** :
   Le démon `flx-netd` surveille l'interface réseau virtuelle, configure l'adresse IP via DHCP et alimente le cache `/run/netinfo` en temps réel.
3. **Journalisation et instrumentation** :
   Toutes les transitions d'état du système (démarrage, verrouillage, bureau, ouverture de fenêtre, changement de thème) émettent des marqueurs standardisés `W12:...` sur la console série `/dev/ttyAMA0` pour permettre l'automatisation et les tests d'intégration sans intrusion visuelle.
