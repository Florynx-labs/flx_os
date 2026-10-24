# FLX OS — plan de migration des applications vers Rust

FLX garde son compositeur maison, son protocole de fenêtres et son rendu sans GTK. La migration doit réutiliser les primitives graphiques FLX (canvas, texte, icônes Tabler, événements et widgets) depuis des interfaces Rust sûres, au lieu d’introduire une pile GTK/Qt parallèle. `taskmgr` et le service `authd` sont déjà en Rust; le reste de la pile demeure mixte.

## Applications candidates

| Priorité | Application | État et intérêt de Rust |
|---|---|---|
| 1 | Paramètres et comptes | C aujourd’hui. Rust convient aux formulaires, à la validation des entrées et aux appels typés vers `authd`; isoler clairement les opérations privilégiées. |
| 2 | Explorateur de fichiers | C aujourd’hui. Rust aide à sécuriser les chemins, les opérations de fichiers, les périphériques et les erreurs d’ouverture. |
| 3 | Terminal | C aujourd’hui. Rust convient au PTY, à la gestion de plusieurs onglets, au scrollback et à l’interprétation des séquences ANSI. |
| 4 | Informations système (`proof`) | C aujourd’hui. Rust facilite la collecte et la présentation structurée des données système; le rendu doit remplacer les longues listes par des vues synthétiques. |
| 5 | Calculatrice et éditeur de texte | C aujourd’hui. Applications petites et réversibles, bonnes candidates pour valider le kit UI Rust. |
| 6 | Navigateur | C aujourd’hui avec moteur HTML/CSS FLX sans JavaScript. Migration possible, mais les défauts actuels viennent d’abord des limites du moteur et de la couverture CSS/HTML. Stabiliser le moteur et les formulaires avant de réécrire le chrome en Rust. |
| 7 | Compositeur / shell | C aujourd’hui. Rust peut renforcer la sûreté mémoire, mais c’est une migration à haut risque : séparer le shell graphique du gestionnaire de fenêtres, puis migrer sous tests de compatibilité. |

## Étapes

1. **Stabiliser le contrat UI Rust.** Exposer les primitives de fenêtre FLX par une API C ABI étroite et documentée; ajouter des types Rust sûrs pour canvas, événements, icônes, texte, clavier et temporisateurs. Garder l’ABI minimale et versionnée.
2. **Migrer les petits outils.** Porter calculatrice et éditeur de texte, puis vérifier clavier, presse-papiers, redimensionnement, thème et multi-utilisateur.
3. **Migrer les applications sensibles aux entrées.** Réécrire Paramètres/comptes et explorateur. Conserver `authd` comme seule autorité pour les secrets; opérations de fichiers sous l’UID de session.
4. **Refaire le terminal avec de vrais onglets.** Un PTY et un état de scrollback par onglet, création/fermeture/sélection sans nouvelle fenêtre, redimensionnement du PTY actif, nettoyage des enfants à la fermeture.
5. **Polir système et performances.** Porter `proof`, regrouper les indicateurs matériels, filtrer les threads noyau et éviter le rafraîchissement de données inchangées.
6. **Améliorer le navigateur avant son port.** Ajouter des pages d’erreur FLX, corriger les formulaires/navigation et compléter HTML/CSS nécessaire; documenter l’absence de JavaScript. Porter ensuite l’interface du navigateur, sans promettre une compatibilité avec les sites modernes avant d’avoir un moteur adapté.
7. **Évaluer la migration du shell.** Après stabilisation de l’ABI et des applications, migrer les composants isolés du shell; conserver DRM, entrée et gestionnaire de fenêtres compatibles à chaque étape.
8. **Mesurer les versions de référence.** Pour chaque migration, comparer démarrage, mémoire, CPU au repos, latence clavier et stabilité sur plusieurs sessions, puis ne remplacer le binaire C qu’après validation fonctionnelle.

## Ordre recommandé

`kit UI Rust → calculatrice/éditeur → paramètres/comptes → explorateur → terminal → informations système → navigateur → shell/compositeur`.

Cette séquence livre rapidement des applications testables, sécurise d’abord les chemins et comptes, puis réserve les composants système les plus risqués pour la fin.
