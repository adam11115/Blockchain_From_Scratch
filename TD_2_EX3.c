/* Exercice 3 : Proof of Stake + comparaison avec Proof of Work */
#include "common.h"

#define MAX_BLOCKS 20
#define NB_VAL 4

typedef struct { char name[20]; int stake; } Validator;

typedef struct {
    int           index;
    long          timestamp;
    char          data[100];
    char          prev_hash[HASH_LEN];
    char          hash[HASH_LEN];
    unsigned long nonce;        /* utilise seulement par la PoW */
    char          validator[20];/* utilise seulement par la PoS */
} Block;

Validator validators[NB_VAL] = {
    {"Alice", 50}, {"Bob", 30}, {"Carol", 15}, {"Dave", 5}
};

void calc_hash(Block *b, char *out) {
    char buf[300];
    sprintf(buf, "%lu|%d|%ld|%s|%s|%s", b->nonce, b->index, b->timestamp,
            b->data, b->prev_hash, b->validator);
    hash_to_hex(buf, out);
}

/* --- PoS : choix aleatoire d'un validateur, pondere par sa mise --- */
int select_validator(void) {
    int total = 0;
    for (int i = 0; i < NB_VAL; i++) total += validators[i].stake;
    int r = rand() % total, sum = 0;
    for (int i = 0; i < NB_VAL; i++) {
        sum += validators[i].stake;
        if (r < sum) return i;
    }
    return NB_VAL - 1;
}

/* PoS : le validateur choisi signe le bloc, un seul calcul de hash */
void forge_pos(Block *b) {
    int v = select_validator();
    strcpy(b->validator, validators[v].name);
    b->nonce = 0;
    calc_hash(b, b->hash);
}

/* PoW : recherche du nonce */
unsigned long mine_pow(Block *b, int diff) {
    strcpy(b->validator, "none");
    b->nonce = 0;
    calc_hash(b, b->hash);
    while (!valid_pow(b->hash, diff)) { b->nonce++; calc_hash(b, b->hash); }
    return b->nonce;
}

void init_block(Block *b, int i, Block *prev) {
    b->index = i;
    b->timestamp = (long)time(NULL);
    sprintf(b->data, "Transaction numero %d", i);
    strcpy(b->prev_hash, prev ? prev->hash : "0000000000000000");
}

int main(void) {
    srand(42);
    int nb_blocks = 5, diff = 5;

    printf("=== Partie 1 : selection des validateurs (1000 tirages) ===\n");
    int wins[NB_VAL] = {0};
    for (int i = 0; i < 1000; i++) wins[select_validator()]++;
    for (int i = 0; i < NB_VAL; i++)
        printf("%-6s mise=%-3d -> choisi %d fois (%.1f%%)\n",
               validators[i].name, validators[i].stake, wins[i], wins[i] / 10.0);

    printf("\n=== Partie 2 : validation de %d blocs avec PoS ===\n", nb_blocks);
    Block pos[MAX_BLOCKS];
    double t0 = now_ms();
    for (int i = 0; i < nb_blocks; i++) {
        init_block(&pos[i], i, i ? &pos[i - 1] : NULL);
        forge_pos(&pos[i]);
    }
    double t_pos = now_ms() - t0;
    for (int i = 0; i < nb_blocks; i++)
        printf("Bloc %d | validateur=%-6s | hash=%s\n", i, pos[i].validator, pos[i].hash);
    printf("Temps PoS : %.4f ms\n", t_pos);

    printf("\n=== Partie 3 : validation de %d blocs avec PoW (difficulte %d) ===\n", nb_blocks, diff);
    Block pow[MAX_BLOCKS];
    t0 = now_ms();
    for (int i = 0; i < nb_blocks; i++) {
        init_block(&pow[i], i, i ? &pow[i - 1] : NULL);
        mine_pow(&pow[i], diff);
    }
    double t_pow = now_ms() - t0;
    for (int i = 0; i < nb_blocks; i++)
        printf("Bloc %d | nonce=%-8lu | hash=%s\n", i, pow[i].nonce, pow[i].hash);
    printf("Temps PoW : %.4f ms\n", t_pow);

    printf("\n=== Comparaison ===\n");
    printf("PoS : %.4f ms\nPoW : %.4f ms\n", t_pos, t_pow);
    if (t_pos < 0.0001) t_pos = 0.0001;
    printf("Le plus rapide : %s (environ %.0f fois plus rapide)\n",
           t_pos < t_pow ? "Proof of Stake" : "Proof of Work",
           t_pos < t_pow ? t_pow / t_pos : t_pos / t_pow);
    return 0;
}