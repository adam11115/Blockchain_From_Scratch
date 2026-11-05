# Blockchain from scratch en C : Atelier 1 et Atelier 2

Ce dépôt contient deux ateliers qui se suivent :

- **Atelier 1** : construire les briques d'une blockchain en C (arbre de Merkle, Proof of Work, Proof of Stake, blockchain complète).
- **Atelier 2** : construire une plateforme de simulation d'attaques sur une blockchain PoW/PoS. Seule l'**étape 1** (le simulateur de réseau) est faite pour l'instant.

Tout est écrit en C simple, sans bibliothèque externe, avec la fonction de hachage **DJB2** de Daniel J. Bernstein.

---

## 1. Organisation des fichiers

| Fichier | Atelier | Rôle |
|---|---|---|
| `common.h` | 1 et 2 | Fonctions partagées : hash DJB2, chronomètre, vérification de la difficulté, racine de Merkle |
| `ex1_merkle.c` | 1 | Exercice 1 : arbre de Merkle |
| `ex2_pow.c` | 1 | Exercice 2 : Proof of Work |
| `ex3_pos.c` | 1 | Exercice 3 : Proof of Stake et comparaison avec la PoW |
| `ex4_blockchain.c` | 1 | Exercice 4 : blockchain complète |
| `A2_step1_simulator.c` | 2 | Étape 1 : simulateur de réseau (nœuds, latence, forks) |
| `A2_step2_attack51.c` | 2 | Étapes 2 et 3 : transactions, soldes et attaque des 51 % (double dépense) |

> Si vos fichiers s'appellent `TD_1_EX1.c`, `TD_1_EX2.c`, `TD_2_EX3.c` et `TD_1_EX4.c`, ce sont les mêmes programmes que `ex1_merkle.c`, `ex2_pow.c`, `ex3_pos.c` et `ex4_blockchain.c`. Tous les `.c` doivent être dans le **même dossier** que `common.h`.

## 2. Compilation et exécution

Il faut un compilateur C (gcc). Sous Windows, installer MSYS2 puis `pacman -S mingw-w64-x86_64-gcc` et ajouter `C:\msys64\mingw64\bin` au PATH.

Dans un terminal ouvert dans le dossier du projet :

```
gcc -O2 -o ex1 ex1_merkle.c
.\ex1

gcc -O2 -o ex2 ex2_pow.c
.\ex2

gcc -O2 -o ex3 ex3_pos.c
.\ex3

gcc -O2 -o ex4 ex4_blockchain.c
.\ex4

gcc -O2 -o sim A2_step1_simulator.c -lm
.\sim

gcc -O2 -o attack51 A2_step2_attack51.c -lm
.\attack51
```

Sous Linux ou macOS, remplacer `.\` par `./`. Le `-lm` est nécessaire pour l'Atelier 2 (fonction `log()`).

---

## 3. Fonction de hachage commune : DJB2

```c
unsigned long long djb2(const char *str) {
    unsigned long long hash = 5381;
    int c;
    while ((c = (unsigned char)*str++))
        hash = ((hash << 5) + hash) + c;   /* hash * 33 + c */
    return hash;
}
```

Le résultat (64 bits) est affiché en 16 caractères hexadécimaux.

**Limite importante :** DJB2 n'est pas une fonction cryptographique. Elle a des collisions et ne doit pas être utilisée en production (on utiliserait SHA-256). Elle sert ici à des fins pédagogiques.

**Remarque sur le minage :** avec DJB2, les derniers caractères de la chaîne hachée influencent très peu les bits de poids fort du hash. Le nonce est donc placé **au début** de la chaîne dans la PoW, sinon le minage serait très irrégulier.

---

## 4. Atelier 1 : les briques d'une blockchain

### Exercice 1 : arbre de Merkle (`ex1_merkle.c`)

**Principe.** Un arbre de Merkle résume plusieurs données par un seul hash, la racine. Chaque feuille est le hash d'une transaction, et chaque nœud parent est le hash de la concaténation de ses deux enfants. Si un niveau a un nombre impair de nœuds, le dernier est dupliqué.

**Fonctions principales :**
- `build_tree()` : construit l'arbre niveau par niveau.
- `get_proof()` : récupère les hashs « frères » le long du chemin d'une feuille.
- `verify_proof()` : recalcule la racine à partir d'une donnée et de sa preuve.

**Ce que le programme démontre :** avec 5 transactions la racine est `cd1d4d33a10dd5c2`. La preuve de `Carol->Dave:3` est valide, celle de `Carol->Dave:300` est rejetée. Modifier une seule transaction change la racine (`172e7713052dfcd1`).

### Exercice 2 : Proof of Work (`ex2_pow.c`)

**Principe.** Pour ajouter un bloc, le mineur cherche un **nonce** tel que le hash du bloc commence par un certain nombre de zéros (la **difficulté**). Trouver le nonce demande beaucoup d'essais, mais le vérifier est instantané.

**Fonctions principales :** `calc_hash()`, `mine()`, `add_block()`, `is_chain_valid()`.

**Exemple de résultats** (varient d'une machine et d'une exécution à l'autre) :

| Difficulté | Nonce moyen | Temps moyen (ms) |
|---|---|---|
| 1 | 16 | 0,004 |
| 2 | 173 | 0,036 |
| 3 | 4 806 | 2,4 |
| 4 | 73 976 | 34,8 |
| 5 | 2 009 225 | 1 139 |

Le temps est multiplié par environ 16 à chaque niveau, car chaque zéro hexadécimal supplémentaire divise la probabilité de succès par 16. Le programme construit aussi une chaîne de 4 blocs, puis détecte la falsification du bloc 1.

### Exercice 3 : Proof of Stake (`ex3_pos.c`)

**Principe.** Le créateur du prochain bloc est un **validateur tiré au sort** avec une probabilité proportionnelle à sa mise. Il n'y a pas de recherche de nonce : un seul calcul de hash suffit.

**Validateurs de l'exemple :** Alice (50), Bob (30), Carol (15), Dave (5). Sur 1 000 tirages, les fréquences obtenues suivent les mises.

**Comparaison pour 5 blocs :** la PoS prend environ 0,005 ms et la PoW (difficulté 5) environ 9 450 ms. La PoS est donc bien plus rapide dans cette implémentation. Ce résultat dépend de l'implémentation et de la difficulté choisie.

### Exercice 4 : blockchain complète (`ex4_blockchain.c`)

Elle réunit tout ce qui précède :
- Des **transactions** (expéditeur, destinataire, montant), jusqu'à 5 par bloc.
- La **racine de Merkle** des transactions dans chaque bloc.
- Le **chaînage par hash** (chaque bloc contient le hash du précédent).
- Un **consensus au choix** : PoW (difficulté 5) ou PoS (validateurs Alice 60, Bob 30, Carol 10).
- `is_valid()` : vérifie la racine de Merkle, le hash, le consensus (preuve de travail ou validateur connu) et le chaînage.

**Trois attaques testées sur chaque chaîne :**

| Attaque | PoW | PoS |
|---|---|---|
| 1. Changer un montant | Merkle incorrect (bloc 1) | Merkle incorrect (bloc 1) |
| 2. Changer un montant et recalculer Merkle et hash | Preuve de travail invalide (bloc 1) | Lien cassé au bloc 2 |
| 3. Changer le `prev_hash` du bloc 3 | Hash incorrect (bloc 3) | Hash incorrect (bloc 3) |

En PoW, un attaquant doit refaire le minage du bloc modifié et de tous les suivants. En PoS, il suffit de recalculer les hashs, et la sécurité repose sur le contrôle des validateurs.

---

## 5. Atelier 2 : plateforme de simulation d'attaques

**Sujet :** concevoir une plateforme expérimentale pour simuler, analyser et détecter des attaques sur une blockchain PoW/PoS : attaque des 51 % avec double dépense, minage égoïste (selfish mining), rétention de blocs (block withholding) dans un pool, et attaque Sybil. Il faut aussi proposer un mécanisme adaptatif de détection, des contre-mesures et une évaluation expérimentale (taux de réussite, précision et rappel, temps de détection, pertes économiques, surcoût).

### Ce qui est fait : étape 1 (`A2_step1_simulator.c`)

Un **simulateur de réseau honnête** : plusieurs nœuds minent, s'envoient leurs blocs avec un délai, créent des forks et se réorganisent. C'est la **blockchain de référence** à laquelle les versions attaquée et protégée seront comparées.

#### Comment ça marche

Le programme est une **simulation à événements discrets**. Il garde une liste d'événements futurs avec leur instant (en secondes) et traite toujours le plus proche :

```
boucle : prendre l'événement le plus proche
   ├─ "bloc trouvé"      -> choisir le mineur (tirage proportionnel à sa puissance de hash),
   │                        créer le bloc sur SA chaîne, l'envoyer aux autres nœuds avec un délai
   └─ "bloc reçu par N"  -> N compare avec sa chaîne, change de chaîne si la nouvelle est plus longue
```

Étapes du code :

1. **Générateur aléatoire** (`rnd`, xorshift64). On n'utilise pas `rand()` car sous Windows il ne donne que 15 bits. Avec une graine fixe (seed), chaque expérience est reproductible.
2. **Structures de données** : `Block` (id, parent, hauteur, mineur, instant, hash), `Node` (part de hash, dernier bloc, blocs connus, statistiques de réorganisation), `Event`.
3. **Minage modélisé** : le temps avant le prochain bloc du réseau suit une loi exponentielle de moyenne 10 s. On ne cherche pas de nonce, ce qui permet de lancer des centaines d'expériences rapidement.
4. **Réseau** : chaque bloc est envoyé aux autres nœuds avec un délai aléatoire de moyenne `latency` (entre 0,5 et 1,5 fois cette valeur). Deux nœuds peuvent trouver un bloc pendant ce délai, ce qui crée un fork.
5. **Réception (`node_accept`)** : le nœud apprend le bloc et ses ancêtres manquants, puis applique la **règle de la chaîne la plus longue** (à égalité, il garde le premier vu). S'il abandonne des blocs, c'est une **réorganisation**, dont on mesure la profondeur.
6. **Métriques** : taux d'orphelins, délai de propagation, nombre et profondeur des réorganisations, part de récompense par mineur, accord final des nœuds.
7. **Vérification d'intégrité** (`verify_chain`) : recalcule les hashs de la chaîne, comme `is_valid()` de l'Atelier 1.

#### Résultats obtenus

**Test 1 : réseau honnête** (5 nœuds, 2 000 blocs, temps de bloc 10 s, latence 1 s)

| Nœud | Part de hash | Blocs minés | Dans la chaîne | Part de récompense |
|---|---|---|---|---|
| N0 | 40 % | 822 | 790 | 42,2 % |
| N1 | 25 % | 524 | 491 | 26,2 % |
| N2 | 15 % | 294 | 268 | 14,3 % |
| N3 | 10 % | 175 | 160 | 8,5 % |
| N4 | 10 % | 185 | 165 | 8,8 % |

- 126 blocs orphelins sur 2 000 (6,3 %), propagation moyenne de 1,30 s.
- Les 5 nœuds terminent sur la même chaîne, et les réorganisations sont peu profondes (2 blocs au maximum).
- La falsification d'un bloc au milieu de la chaîne est détectée par la vérification des hashs.
- **À noter :** le plus gros mineur gagne légèrement plus que sa part de hash, et les petits un peu moins, car les gros mineurs subissent moins d'orphelins sur leurs propres blocs. Le minage égoïste exploite justement cet effet.

**Test 2 : influence de la latence** (moyenne sur 10 simulations, écrite dans `latency_sweep.csv`)

| Latence (s) | Orphelins (%) | Propagation (s) | Réorg. max |
|---|---|---|---|
| 0 | 0,00 | 0,00 | 0 |
| 0,5 | 3,46 | 0,65 | 2 |
| 1 | 6,58 | 1,30 | 3 |
| 2 | 12,19 | 2,59 | 4 |
| 5 | 23,81 | 6,46 | 6 |
| 10 | 35,02 | 12,84 | 7 |

Ces valeurs décrivent le **comportement normal** du réseau. Le détecteur de l'Atelier 2 s'en servira comme référence pour repérer ce qui s'écarte de la normale.

### Ce qui est fait : étapes 2 et 3 (`A2_step2_attack51.c`)

Le même simulateur, enrichi de **transactions avec soldes** et de l'**attaque des 51 % avec double dépense**.

**Étape 2 : transactions et soldes.** Chaque bloc contient au plus une transaction (expéditeur, destinataire, montant). `balance()` calcule le solde d'un compte en parcourant la chaîne. Un bloc dont la transaction dépense plus que le solde est marqué invalide et **rejeté** par les nœuds. Le Test 0 le montre : on ne peut pas dépenser deux fois les mêmes pièces sur une seule chaîne.

**Étape 3 : attaque des 51 %.** Le scénario :
1. L'attaquant possède 100 pièces. Il envoie les 100 au commercant (transaction **T1**), que les nœuds honnêtes incluent dans un bloc.
2. En parallèle, il mine **en secret** une chaîne privée depuis le début, dont le premier bloc contient **T2** : les mêmes 100 pièces envoyées à son propre second compte.
3. Le commercant livre la marchandise après *z* confirmations de T1.
4. Dès que la chaîne privée est **strictement plus longue** que la chaîne publique, l'attaquant la publie. Les nœuds honnêtes se réorganisent, T1 disparaît et T2 devient valide : le commercant a livré sans être payé.
5. L'attaquant abandonne s'il a 15 blocs de retard.

Le paramètre `c.attack = 1` active l'attaque, `c.attacker_share` donne sa part de hash et `c.confirmations` le nombre de confirmations exigées.

**Résultats : taux de réussite (300 simulations par case, latence 1 s)**, format *simulé % (théorie %)* :

| Attaquant | z=1 | z=2 | z=3 | z=6 | z=10 |
|---|---|---|---|---|---|
| 10 % | 4,3 (2,8) | 0,7 (0,7) | 0,3 (0,2) | 0,0 (0,0) | 0,0 (0,0) |
| 20 % | 14,3 (12,4) | 9,0 (6,2) | 4,7 (3,1) | 1,0 (0,4) | 0,0 (0,0) |
| 30 % | 31,3 (30,9) | 23,0 (22,3) | 18,0 (16,3) | 9,7 (6,6) | 2,7 (2,1) |
| 40 % | 63,0 (60,1) | 55,7 (54,1) | 53,0 (49,0) | 42,3 (37,3) | 29,7 (26,6) |
| 45 % | 78,0 (78,8) | 80,0 (75,9) | 71,7 (73,3) | 66,7 (66,8) | 63,0 (59,8) |
| 51 % | 95,7 (100) | 92,7 (100) | 94,3 (100) | 95,3 (100) | 95,0 (100) |
| 60 % | 99,7 (100) | 100 (100) | 100 (100) | 100 (100) | 99,7 (100) |

La colonne « théorie » est la formule de Rosenfeld pour la probabilité qu'un attaquant rattrape la chaîne publique après *z* confirmations. Elle sert à **valider le simulateur**.

**Lecture des résultats :**
- Plus l'attaquant a de puissance, plus l'attaque réussit. Plus le commercant attend de confirmations, moins elle réussit (à 30 %, de 31 % avec 1 confirmation à 2,7 % avec 10).
- Le simulateur suit la théorie de près. Les petits écarts (simulé un peu au-dessus) viennent de la latence réseau : elle crée des blocs orphelins honnêtes, donc la chaîne publique avance un peu moins vite que dans le modèle idéal.
- À 51 % et plus, la théorie donne 100 %, mais le simulateur donne environ 95 % : l'attaquant abandonne à 15 blocs de retard, alors qu'avec 51 % la marche aléatoire est presque plate et peut demander une patience illimitée.
- Les résultats sont écrits dans `attack51_results.csv`.

---

## 6. Comment l'Atelier 2 se rattache à l'Atelier 1

Le simulateur est un **programme nouveau**, pas un import direct du code de l'Atelier 1. Le lien est le suivant :

**Ce qui est réutilisé**
- `common.h` : la fonction de hachage **DJB2** (hash des blocs et vérification de la chaîne).
- Les **concepts** : blocs chaînés par hash, règle de la chaîne la plus longue, vérification d'intégrité, consensus PoW et PoS.

**Ce qui est nouveau**
- Plusieurs nœuds, chacun avec sa propre vue de la chaîne.
- Un réseau avec latence, donc des forks et des réorganisations.
- Un minage **simulé** (tirage aléatoire) au lieu de la recherche de nonce de l'Exercice 2, qui serait trop lente pour des centaines d'expériences (la difficulté 5 prenait déjà environ une seconde par bloc).
- Les transactions, les soldes et l'attaque des 51 % sont ajoutés à l'étape 2-3. Les autres attaques et le détecteur restent à faire.

**Formulation pour le rapport :** la blockchain de l'Atelier 1 (blocs, chaînage DJB2, consensus PoW et PoS) est le modèle de référence, et l'Atelier 2 l'enveloppe dans un simulateur de réseau pour y injecter des attaques.

---

## 7. Prochaines étapes

| Étape | Contenu |
|---|---|
| 2 | ✔ Fait : transactions et soldes |
| 3 | ✔ Fait : **attaque des 51 %** avec double dépense, taux de réussite selon la part de hash et les confirmations |
| 4 | **Minage égoïste** : publication stratégique des blocs. Comparer la part de récompense de l'attaquant à sa part de hash |
| 5 | **Block withholding** dans un pool : un membre cache les blocs gagnants. Mesure de la perte du pool |
| 6 | **Attaque Sybil** : de nombreux faux nœuds dans un réseau P2P (nécessite un graphe de pairs) |
| 7 | **Détecteur adaptatif** : surveille le taux d'orphelins, le délai de propagation, la distribution des récompenses et la profondeur des réorganisations, avec des seuils qui s'adaptent à la normale du réseau. Option : un modèle de machine learning |
| 8 | **Contre-mesures** adaptées à chaque attaque (plus de confirmations, départage aléatoire, pénalités dans le pool, limite de pairs par origine) |
| 9 | **Expériences et rapport** : comparaison référence / attaquée / protégée, avec taux de réussite, précision et rappel, temps de détection, pertes économiques et surcoût. Plusieurs exécutions par configuration avec des graines fixes |

Les attaques seront activées par un paramètre de la simulation, par exemple :

```c
c.attack = ATTACK_51;       /* ou ATTACK_SELFISH, ATTACK_WITHHOLD, ATTACK_SYBIL */
c.attacker_share = 30;      /* part de hash de l'attaquant, en % */
sim_run(&c, seed, &result);
```

---

## 8. Limites connues

- DJB2 n'est pas cryptographique (collisions possibles).
- La PoS est simplifiée : pas de pénalité pour un validateur malhonnête, pas de réseau distribué.
- Un bloc ne contient qu'une transaction, et l'attaquant du scénario est modélisé avec une stratégie simple (publier dès qu'il est strictement plus long, abandonner à 15 blocs de retard).
- Le minage du simulateur est modélisé par une loi exponentielle, pas calculé par hachage.
- Les nœuds apprennent automatiquement les ancêtres manquants d'un bloc reçu (pas de protocole de synchronisation).
- Les temps mesurés dépendent de la machine et varient d'une exécution à l'autre.