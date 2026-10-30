/* Exercice 4 : Blockchain simple avec PoW et PoS, transactions et Merkle */
#include "common.h"

#define MAX_BLOCKS 20
#define MAX_TX     5
#define NB_VAL     3

enum { MODE_POW, MODE_POS };

typedef struct { char sender[20]; char receiver[20]; int amount; } Transaction;

typedef struct {
    int           index;
    long          timestamp;
    Transaction   tx[MAX_TX];
    int           tx_count;
    char          merkle[HASH_LEN];
    char          prev_hash[HASH_LEN];
    char          hash[HASH_LEN];
    unsigned long nonce;
    char          validator[20];
} Block;

typedef struct {
    Block blocks[MAX_BLOCKS];
    int   length;
    int   mode;
    int   difficulty;
    Transaction pending[MAX_TX];   /* transactions en attente */
    int   pending_count;
} Blockchain;

const char *val_name[NB_VAL]  = {"Alice", "Bob", "Carol"};
int         val_stake[NB_VAL] = {60, 30, 10};

/* ---------- Utilitaires ---------- */
void tx_to_string(Transaction *t, char *out) {
    sprintf(out, "%s->%s:%d", t->sender, t->receiver, t->amount);
}

void compute_merkle(Block *b, char out[HASH_LEN]) {
    char strs[MAX_TX][TX_STR];
    for (int i = 0; i < b->tx_count; i++) tx_to_string(&b->tx[i], strs[i]);
    merkle_root(strs, b->tx_count, out);
}

void calc_hash(Block *b, char *out) {
    char buf[300];
    sprintf(buf, "%lu|%d|%ld|%s|%s|%s", b->nonce, b->index, b->timestamp,
            b->merkle, b->prev_hash, b->validator);
    hash_to_hex(buf, out);
}

int select_validator(void) {
    int total = 0, sum = 0;
    for (int i = 0; i < NB_VAL; i++) total += val_stake[i];
    int r = rand() % total;
    for (int i = 0; i < NB_VAL; i++) { sum += val_stake[i]; if (r < sum) return i; }
    return NB_VAL - 1;
}

int is_validator(const char *name) {
    for (int i = 0; i < NB_VAL; i++) if (strcmp(val_name[i], name) == 0) return 1;
    return 0;
}

/* ---------- Blockchain ---------- */
void init_chain(Blockchain *c, int mode, int difficulty) {
    c->length = 0; c->mode = mode; c->difficulty = difficulty; c->pending_count = 0;
}

int add_transaction(Blockchain *c, const char *from, const char *to, int amount) {
    if (c->pending_count >= MAX_TX) return 0;
    Transaction *t = &c->pending[c->pending_count++];
    strcpy(t->sender, from); strcpy(t->receiver, to); t->amount = amount;
    return 1;
}

/* Cree un bloc avec les transactions en attente et le valide (PoW ou PoS) */
void create_block(Blockchain *c) {
    Block *b = &c->blocks[c->length];
    b->index = c->length;
    b->timestamp = (long)time(NULL);
    b->tx_count = c->pending_count;
    for (int i = 0; i < c->pending_count; i++) b->tx[i] = c->pending[i];
    c->pending_count = 0;
    compute_merkle(b, b->merkle);
    strcpy(b->prev_hash, c->length ? c->blocks[c->length - 1].hash : "0000000000000000");
    b->nonce = 0;

    if (c->mode == MODE_POW) {
        strcpy(b->validator, "none");
        calc_hash(b, b->hash);
        while (!valid_pow(b->hash, c->difficulty)) { b->nonce++; calc_hash(b, b->hash); }
    } else {
        strcpy(b->validator, val_name[select_validator()]);
        calc_hash(b, b->hash);
    }
    c->length++;
}

/* Verification complete de l'integrite */
int is_valid(Blockchain *c) {
    for (int i = 0; i < c->length; i++) {
        Block *b = &c->blocks[i];
        char tmp[HASH_LEN];

        compute_merkle(b, tmp);
        if (strcmp(tmp, b->merkle) != 0) {
            printf("   -> Bloc %d : racine de Merkle incorrecte (transactions modifiees)\n", i);
            return 0;
        }
        calc_hash(b, tmp);
        if (strcmp(tmp, b->hash) != 0) {
            printf("   -> Bloc %d : hash incorrect\n", i);
            return 0;
        }
        if (c->mode == MODE_POW && !valid_pow(b->hash, c->difficulty)) {
            printf("   -> Bloc %d : preuve de travail invalide\n", i);
            return 0;
        }
        if (c->mode == MODE_POS && !is_validator(b->validator)) {
            printf("   -> Bloc %d : validateur inconnu\n", i);
            return 0;
        }
        if (i > 0 && strcmp(b->prev_hash, c->blocks[i - 1].hash) != 0) {
            printf("   -> Bloc %d : prev_hash ne correspond pas au bloc %d\n", i, i - 1);
            return 0;
        }
    }
    return 1;
}

void print_chain(Blockchain *c) {
    for (int i = 0; i < c->length; i++) {
        Block *b = &c->blocks[i];
        printf("Bloc %d | tx=%d | merkle=%s | prev=%s | hash=%s | ", i, b->tx_count,
               b->merkle, b->prev_hash, b->hash);
        if (c->mode == MODE_POW) printf("nonce=%lu\n", b->nonce);
        else                     printf("validateur=%s\n", b->validator);
    }
}

/* Construit une chaine de 5 blocs */
void build_demo(Blockchain *c) {
    add_transaction(c, "System", "Alice", 100);
    create_block(c);
    add_transaction(c, "Alice", "Bob", 30);
    add_transaction(c, "Alice", "Carol", 20);
    create_block(c);
    add_transaction(c, "Bob", "Dave", 10);
    add_transaction(c, "Carol", "Eve", 5);
    add_transaction(c, "Bob", "Carol", 7);
    create_block(c);
    add_transaction(c, "Dave", "Alice", 4);
    create_block(c);
    add_transaction(c, "Eve", "Bob", 2);
    add_transaction(c, "Alice", "Eve", 15);
    create_block(c);
}

/* Teste 3 attaques sur une copie de la chaine */
void test_tampering(Blockchain *orig) {
    Blockchain c;

    printf("\n[Attaque 1] Modifier un montant (bloc 1, tx 0 : 30 -> 3000)\n");
    c = *orig;
    c.blocks[1].tx[0].amount = 3000;
    printf("   Chaine valide ? %s\n", is_valid(&c) ? "OUI" : "NON");

    printf("\n[Attaque 2] Modifier + recalculer Merkle et hash du bloc 1 (sans refaire le consensus)\n");
    c = *orig;
    c.blocks[1].tx[0].amount = 3000;
    compute_merkle(&c.blocks[1], c.blocks[1].merkle);
    calc_hash(&c.blocks[1], c.blocks[1].hash);
    printf("   Chaine valide ? %s\n", is_valid(&c) ? "OUI" : "NON");

    printf("\n[Attaque 3] Supprimer le lien : changer prev_hash du bloc 3\n");
    c = *orig;
    strcpy(c.blocks[3].prev_hash, "deadbeefdeadbeef");
    printf("   Chaine valide ? %s\n", is_valid(&c) ? "OUI" : "NON");
}

int main(void) {
    srand(7);
    static Blockchain pow_chain, pos_chain;
    int diff = 5;

    /* ---------- PoW ---------- */
    printf("############ BLOCKCHAIN AVEC PROOF OF WORK (difficulte %d) ############\n", diff);
    init_chain(&pow_chain, MODE_POW, diff);
    double t0 = now_ms();
    build_demo(&pow_chain);
    double t_pow = now_ms() - t0;
    print_chain(&pow_chain);
    printf("\nChaine valide ? %s\n", is_valid(&pow_chain) ? "OUI" : "NON");
    printf("Temps de construction : %.3f ms\n", t_pow);
    test_tampering(&pow_chain);

    /* ---------- PoS ---------- */
    printf("\n\n############ BLOCKCHAIN AVEC PROOF OF STAKE ############\n");
    printf("Validateurs : Alice(60) Bob(30) Carol(10)\n");
    init_chain(&pos_chain, MODE_POS, 0);
    t0 = now_ms();
    build_demo(&pos_chain);
    double t_pos = now_ms() - t0;
    print_chain(&pos_chain);
    printf("\nChaine valide ? %s\n", is_valid(&pos_chain) ? "OUI" : "NON");
    printf("Temps de construction : %.3f ms\n", t_pos);
    test_tampering(&pos_chain);

    /* ---------- Comparaison ---------- */
    printf("\n\n############ COMPARAISON ############\n");
    printf("%-18s %-16s %-16s\n", "", "Proof of Work", "Proof of Stake");
    printf("%-18s %-16.3f %-16.3f\n", "Temps (ms)", t_pow, t_pos);
    printf("%-18s %-16s %-16s\n", "Calcul necessaire", "Eleve (nonce)", "Quasi nul");
    printf("%-18s %-16s %-16s\n", "Choix du createur", "Le + rapide", "Selon la mise");
    return 0;
}