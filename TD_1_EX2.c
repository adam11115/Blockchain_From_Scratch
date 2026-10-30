/* Exercice 2 : Proof of Work (blocs + chaine + difficulte variable) */
#include "common.h"

#define MAX_BLOCKS 20

typedef struct {
    int           index;
    long          timestamp;
    char          data[100];
    char          prev_hash[HASH_LEN];
    char          hash[HASH_LEN];
    unsigned long nonce;
} Block;

Block chain[MAX_BLOCKS];
int   chain_len = 0;

/* Le nonce est place au debut du texte pour bien "melanger" le hash DJB2 */
void calc_hash(Block *b, char *out) {
    char buf[300];
    sprintf(buf, "%lu|%d|%ld|%s|%s", b->nonce, b->index, b->timestamp, b->data, b->prev_hash);
    hash_to_hex(buf, out);
}

/* Minage : on augmente le nonce jusqu'a obtenir 'diff' zeros au debut du hash */
void mine(Block *b, int diff) {
    b->nonce = 0;
    calc_hash(b, b->hash);
    while (!valid_pow(b->hash, diff)) {
        b->nonce++;
        calc_hash(b, b->hash);
    }
}

void add_block(const char *data, int diff) {
    Block *b = &chain[chain_len];
    b->index     = chain_len;
    b->timestamp = (long)time(NULL);
    strncpy(b->data, data, 99);
    b->data[99] = '\0';
    strcpy(b->prev_hash, chain_len == 0 ? "0000000000000000" : chain[chain_len - 1].hash);
    mine(b, diff);
    chain_len++;
}

int is_chain_valid(int diff) {
    for (int i = 0; i < chain_len; i++) {
        char h[HASH_LEN];
        calc_hash(&chain[i], h);
        if (strcmp(h, chain[i].hash) != 0) {
            printf("  -> Bloc %d : hash incorrect\n", i); return 0;
        }
        if (!valid_pow(chain[i].hash, diff)) {
            printf("  -> Bloc %d : preuve de travail invalide\n", i); return 0;
        }
        if (i > 0 && strcmp(chain[i].prev_hash, chain[i - 1].hash) != 0) {
            printf("  -> Bloc %d : lien avec le bloc precedent casse\n", i); return 0;
        }
    }
    return 1;
}

int main(void) {
    printf("=== Partie 1 : temps de minage selon la difficulte ===\n");
    printf("(moyenne sur 3 blocs par niveau)\n\n");
    printf("%-11s %-14s %-12s\n", "Difficulte", "Nonce moyen", "Temps (ms)");
    printf("------------------------------------------\n");
    for (int d = 1; d <= 5; d++) {
        double total_t = 0; unsigned long total_n = 0;
        for (int k = 0; k < 3; k++) {
            Block b;
            b.index = k; b.timestamp = (long)time(NULL);
            sprintf(b.data, "Test bloc %d", k);
            strcpy(b.prev_hash, "0000000000000000");
            double t0 = now_ms();
            mine(&b, d);
            total_t += now_ms() - t0;
            total_n += b.nonce;
        }
        printf("%-11d %-14lu %-12.3f\n", d, total_n / 3, total_t / 3);
    }

    printf("\n=== Partie 2 : chaine PoW (difficulte 3) ===\n");
    int diff = 3;
    add_block("Genesis Block", diff);
    add_block("Alice->Bob:10", diff);
    add_block("Bob->Carol:5", diff);
    add_block("Carol->Dave:3", diff);
    for (int i = 0; i < chain_len; i++)
        printf("Bloc %d | nonce=%-6lu | prev=%s | hash=%s\n",
               chain[i].index, chain[i].nonce, chain[i].prev_hash, chain[i].hash);

    printf("\nChaine valide ? %s\n", is_chain_valid(diff) ? "OUI" : "NON");

    printf("\n--- Falsification du bloc 1 (10 -> 1000) ---\n");
    strcpy(chain[1].data, "Alice->Bob:1000");
    printf("Chaine valide ? %s\n", is_chain_valid(diff) ? "OUI" : "NON");
    return 0;
}