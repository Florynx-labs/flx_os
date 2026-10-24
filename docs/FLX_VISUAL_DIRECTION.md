# FLX OS — Direction visuelle

FLX reprend de GNOME la retenue, l'espace et la navigation par vue d'ensemble. FLX garde sa propre identité : renard, ambre, graphite, turquoise et fonds FLX. L'objectif est une interface maison, légère et utilisable au clavier, sans adopter les conventions visuelles de Windows.

## Principes

- Le bureau reste un canevas calme : pas d'icônes fixes, de widget météo permanent ou de barre latérale persistante.
- La vue d'ensemble rassemble recherche, favoris et fenêtres ouvertes. Super/Echap servent à l'ouvrir et la fermer.
- Le dock se masque par défaut et réapparaît au bord inférieur. Les favoris initiaux sont Fichiers, Terminal, Paramètres, Éditeur, Calculatrice et Gestionnaire de tâches.
- La barre supérieure reste mince : recherche à gauche, état système et horloge à droite. Date, météo et énergie apparaissent dans le panneau de l'horloge.
- Les fenêtres gardent les contrôles nécessaires, avec des surfaces plus calmes et une hiérarchie typographique courte.
- Le thème repose sur graphite profond et surfaces neutres. L'ambre signale la marque et les actions principales ; le turquoise indique les états système positifs.

## Typographie

Liberation Sans devient la police d'interface par défaut. Selawik reste disponible pour les utilisateurs qui la préfèrent, avec Liberation Serif et Liberation Mono comme alternatives. Le terminal conserve une police monospace lisible. Les anciens réglages enregistrés sont migrés pour conserver le choix antérieur de l'utilisateur.

## Applications de base

Le jeu de départ reste limité aux applications intégrées utiles : Fichiers, Paramètres, Terminal, Éditeur de texte, Calculatrice et Gestionnaire de tâches. Les outils expérimentaux ou de loisir doivent devenir des installations Getra séparées quand le format de paquets signé sera prêt. L'OS doit démarrer sans boutique ni navigateur tiers préinstallés.

## Stabilité et performance

Le shell et les applications gardent le rendu FLX natif en C/Rust, sans GTK. Les animations doivent rester courtes et éviter les recalculs de flou ou de miniatures à chaque image. Chaque application centrale doit gérer les erreurs de fichiers, les tailles de fenêtre réduites, la navigation clavier, et un arrêt propre. Une fonctionnalité n'est livrée par défaut qu'après une vérification de compilation, démarrage QEMU et flux d'utilisation principal.
