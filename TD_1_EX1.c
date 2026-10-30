//  Arbre de Merkle basique (hash DJB2) 
#include "common.h"

#define MAX_LEVELS 10
#define MAX_NODES  64

typedef struct {
    char h[MAX_LEVELS][MAX_NODES][HASH_LEN]; /* h[niveau][noeud] */
    int  count[MAX_LEVELS];                  /* nombre de noeuds par niveau */
    int  levels;                             /* nombre de niveaux */
} MerkleTree;

typedef struct {
    char hash[MAX_LEVELS][HASH_LEN];  /* hashs "frères" le long du chemin */
    int  is_left[MAX_LEVELS];         /* 1 si le frère est à gauche */
    int  len;
} Proof;

void build_tree(MerkleTree *t, char *data[], int n) {
    t->count[0] = n;
    for (int i = 0; i < n; i++) hash_to_hex(data[i], t->h[0][i]);

    int lvl = 0;
    while (t->count[lvl] > 1) {
        int c = t->count[lvl];
        if (c % 2) {                                  /* impair : duplication */
            strcpy(t->h[lvl][c], t->h[lvl][c - 1]);
            c++;
            t->count[lvl] = c;
        }
        for (int i = 0; i < c; i += 2) {
            char buf[2 * HASH_LEN];
            sprintf(buf, "%.16s%.16s", t->h[lvl][i], t->h[lvl][i + 1]);
            hash_to_hex(buf, t->h[lvl + 1][i / 2]);
        }
        t->count[lvl + 1] = c / 2;
        lvl++;
    }
    t->levels = lvl + 1;
}

void print_tree(MerkleTree *t) {
    for (int l = t->levels - 1; l >= 0; l--) {
        printf("  Niveau %d : ", l);
        for (int i = 0; i < t->count[l]; i++) printf("[%s] ", t->h[l][i]);
        printf("\n");
    }
}

/* Preuve de Merkle pour la feuille d'indice idx */
void get_proof(MerkleTree *t, int idx, Proof *p) {
    p->len = 0;
    for (int l = 0; l < t->levels - 1; l++) {
        int sib = idx ^ 1;                            /* indice du frère */
        strcpy(p->hash[p->len], t->h[l][sib]);
        p->is_left[p->len] = (sib < idx);
        p->len++;
        idx /= 2;
    }
}

/* Vérifie qu'une donnée appartient à l'arbre de racine 'root' */
int verify_proof(const char *data, Proof *p, const char *root) {
    char cur[HASH_LEN], buf[2 * HASH_LEN];
    hash_to_hex(data, cur);
    for (int i = 0; i < p->len; i++) {
        if (p->is_left[i]) sprintf(buf, "%s%s", p->hash[i], cur);
        else               sprintf(buf, "%s%s", cur, p->hash[i]);
        hash_to_hex(buf, cur);
    }
    return strcmp(cur, root) == 0;
}

int main(void) {
    char *tx[] = { "Alice->Bob:10", "Bob->Carol:5", "Carol->Dave:3",
                   "Dave->Eve:8",   "Eve->Alice:2" };
    int n = 5;
    MerkleTree t;

    printf("=== Test 1 : arbre de %d transactions (nombre impair) ===\n", n);
    build_tree(&t, tx, n);
    print_tree(&t);
    char *root = t.h[t.levels - 1][0];
    printf("  Racine de Merkle : %s\n\n", root);

    printf("=== Test 2 : preuve d'appartenance de \"%s\" ===\n", tx[2]);
    Proof p;
    get_proof(&t, 2, &p);
    printf("  Preuve (%d hashs) : ", p.len);
    for (int i = 0; i < p.len; i++) printf("%s ", p.hash[i]);
    printf("\n  Verification donnee originale : %s\n",
           verify_proof(tx[2], &p, root) ? "VALIDE" : "INVALIDE");
    printf("  Verification \"Carol->Dave:300\" : %s\n\n",
           verify_proof("Carol->Dave:300", &p, root) ? "VALIDE" : "INVALIDE");

    printf("=== Test 3 : modification d'une transaction ===\n");
    char root_orig[HASH_LEN];
    strcpy(root_orig, root);
    tx[1] = "Bob->Carol:500";
    MerkleTree t2;
    build_tree(&t2, tx, n);
    printf("  Racine avant : %s\n  Racine apres : %s\n", root_orig, t2.h[t2.levels - 1][0]);
    printf("  Modification detectee : %s\n",
           strcmp(root_orig, t2.h[t2.levels - 1][0]) != 0 ? "OUI" : "NON");
    return 0;
}