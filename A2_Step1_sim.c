/* Atelier 2 - Etape 1 : simulateur de reseau blockchain
   Noeuds + latence reseau + forks + reorganisation de chaine.
   Reseau HONNETE uniquement (les attaques viendront aux etapes suivantes). */
#include "common.h"
#include <math.h>

#define MAX_NODES  20
#define MAX_BLOCKS 6000
#define MAX_EVENTS 4000

/* ================= 1. Generateur aleatoire =================
   On n'utilise pas rand() : sous Windows il ne donne que 15 bits.
   xorshift64 donne les memes resultats sur tous les systemes. */
static unsigned long long rng_state = 1;

double rnd(void) {                       /* nombre dans [0,1) */
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (rng_state >> 11) / 9007199254740992.0;
}
void rng_seed(unsigned long long s) {
    rng_state = s ? s : 1;
    for (int i = 0; i < 10; i++) rnd();
}
double exp_sample(double mean) {         /* loi exponentielle de moyenne 'mean' */
    return -mean * log(1.0 - rnd());
}

/* ================= 2. Structures de donnees ================= */
typedef struct {
    int    id, parent, height, miner;    /* parent = id du bloc precedent (-1 pour la genese) */
    double time;                         /* instant de minage */
    char   hash[HASH_LEN];
    double last_received;                /* instant ou le dernier noeud l'a connu */
} Block;

typedef struct {
    double share;                        /* part de puissance de hash (0..1) */
    int    tip;                          /* id du dernier bloc de sa chaine */
    char   known[MAX_BLOCKS];            /* known[b] = 1 si le noeud connait le bloc b */
    int    reorgs, max_reorg;            /* statistiques de reorganisation */
} Node;

enum { EV_MINE, EV_RECEIVE };
typedef struct { double time; int type, node, block; } Event;

typedef struct {                         /* parametres d'une simulation */
    int    n_nodes;
    double shares[MAX_NODES];            /* en pourcentage */
    double block_time;                   /* temps moyen entre blocs (s) */
    double latency;                      /* delai moyen de propagation (s) */
    int    target_blocks;                /* nombre de blocs a miner */
} Config;

typedef struct {                         /* resultats d'une simulation */
    int    mined, main_len, orphans, agree, max_reorg;
    double orphan_rate, avg_prop;
    int    mined_by[MAX_NODES], main_by[MAX_NODES], reorgs[MAX_NODES];
} Result;

static Block blocks[MAX_BLOCKS];  static int block_count;
static Node  nodes[MAX_NODES];    static int n_nodes;
static Event queue[MAX_EVENTS];   static int qlen;

/* ================= 3. File d'evenements ================= */
void push_event(double t, int type, int node, int block) {
    queue[qlen].time = t; queue[qlen].type = type;
    queue[qlen].node = node; queue[qlen].block = block;
    qlen++;
}
int pop_event(Event *e) {                /* retire l'evenement le plus proche dans le temps */
    if (qlen == 0) return 0;
    int best = 0;
    for (int i = 1; i < qlen; i++) if (queue[i].time < queue[best].time) best = i;
    *e = queue[best];
    queue[best] = queue[--qlen];
    return 1;
}

/* ================= 4. Blocs et hash ================= */
void compute_block_hash(int id, char out[HASH_LEN]) {
    char buf[200];
    Block *b = &blocks[id];
    sprintf(buf, "%.16s|%d|%d|%.3f", blocks[b->parent].hash, b->height, b->miner, b->time);
    hash_to_hex(buf, out);
}

int new_block(int parent, int miner, double time) {
    Block *b = &blocks[block_count];
    b->id = block_count;  b->parent = parent;  b->miner = miner;  b->time = time;
    b->height = blocks[parent].height + 1;
    b->last_received = time;
    compute_block_hash(b->id, b->hash);
    return block_count++;
}

/* Verifie les hash de la chaine jusqu'a 'tip'. Retourne le 1er bloc faux, ou -1 si tout est bon */
int verify_chain(int tip) {
    for (int x = tip; blocks[x].parent != -1; x = blocks[x].parent) {
        char h[HASH_LEN];
        compute_block_hash(x, h);
        if (strcmp(h, blocks[x].hash) != 0) return x;
    }
    return -1;
}

/* ================= 5. Reception d'un bloc + choix de chaine ================= */
void node_accept(int n, int b, double now) {
    Node *nd = &nodes[n];
    if (nd->known[b]) return;

    /* le noeud apprend le bloc ET ses ancetres qu'il ne connaissait pas encore */
    for (int x = b; x != -1 && !nd->known[x]; x = blocks[x].parent) {
        nd->known[x] = 1;
        if (now > blocks[x].last_received) blocks[x].last_received = now;
    }

    /* regle de la chaine la plus longue (a egalite, on garde le premier vu) */
    if (blocks[b].height > blocks[nd->tip].height) {
        int x = nd->tip, y = b;                       /* recherche de l'ancetre commun */
        while (blocks[x].height > blocks[y].height) x = blocks[x].parent;
        while (blocks[y].height > blocks[x].height) y = blocks[y].parent;
        while (x != y) { x = blocks[x].parent; y = blocks[y].parent; }
        int depth = blocks[nd->tip].height - blocks[x].height;  /* blocs abandonnes */
        if (depth > 0) {
            nd->reorgs++;
            if (depth > nd->max_reorg) nd->max_reorg = depth;
        }
        nd->tip = b;
    }
}

/* ================= 6. Simulation ================= */
void sim_run(Config *c, unsigned long long seed, Result *r) {
    rng_seed(seed);
    block_count = 0;  qlen = 0;  n_nodes = c->n_nodes;

    /* bloc de genese */
    Block *g = &blocks[0];
    g->id = 0; g->parent = -1; g->height = 0; g->miner = -1; g->time = 0; g->last_received = 0;
    hash_to_hex("genesis", g->hash);
    block_count = 1;

    double total = 0;
    for (int i = 0; i < n_nodes; i++) total += c->shares[i];
    for (int i = 0; i < n_nodes; i++) {
        memset(&nodes[i], 0, sizeof(Node));
        nodes[i].share = c->shares[i] / total;
        nodes[i].tip = 0;
        nodes[i].known[0] = 1;
    }

    memset(r, 0, sizeof(Result));
    push_event(exp_sample(c->block_time), EV_MINE, -1, -1);

    Event e;
    while (pop_event(&e)) {
        if (e.type == EV_MINE) {
            /* qui a trouve le bloc ? tirage proportionnel a la puissance de hash */
            double roll = rnd(), sum = 0;
            int m = n_nodes - 1;
            for (int i = 0; i < n_nodes; i++) {
                sum += nodes[i].share;
                if (roll < sum) { m = i; break; }
            }
            int b = new_block(nodes[m].tip, m, e.time);   /* mine sur SA chaine */
            r->mined++;  r->mined_by[m]++;
            node_accept(m, b, e.time);                    /* il connait son bloc tout de suite */
            for (int k = 0; k < n_nodes; k++) {           /* diffusion avec delai aleatoire */
                if (k == m) continue;
                double delay = c->latency * (0.5 + rnd());
                push_event(e.time + delay, EV_RECEIVE, k, b);
            }
            if (r->mined < c->target_blocks)
                push_event(e.time + exp_sample(c->block_time), EV_MINE, -1, -1);
        } else {
            node_accept(e.node, e.block, e.time);
        }
    }

    /* ---- statistiques finales ---- */
    int ref = nodes[0].tip;                               /* chaine de reference : celle du noeud 0 */
    for (int x = ref; x != 0; x = blocks[x].parent) { r->main_len++; r->main_by[blocks[x].miner]++; }
    r->orphans = r->mined - r->main_len;
    r->orphan_rate = (double)r->orphans / r->mined;
    for (int i = 0; i < n_nodes; i++) {
        if (nodes[i].tip == ref) r->agree++;
        r->reorgs[i] = nodes[i].reorgs;
        if (nodes[i].max_reorg > r->max_reorg) r->max_reorg = nodes[i].max_reorg;
    }
    double s = 0;
    for (int i = 1; i < block_count; i++) s += blocks[i].last_received - blocks[i].time;
    r->avg_prop = s / (block_count - 1);
}

/* ================= 7. Tests ================= */
int main(void) {
    Config c;
    memset(&c, 0, sizeof(c));
    c.n_nodes = 5;
    double sh[5] = {40, 25, 15, 10, 10};
    for (int i = 0; i < 5; i++) c.shares[i] = sh[i];
    c.block_time = 10.0;
    c.latency = 1.0;
    c.target_blocks = 2000;

    /* ---------- Test 1 : reseau honnete ---------- */
    printf("=== Test 1 : reseau honnete (5 noeuds, %d blocs, temps bloc=%.0fs, latence=%.1fs) ===\n\n",
           c.target_blocks, c.block_time, c.latency);
    static Result r;
    sim_run(&c, 1, &r);

    printf("%-6s %-12s %-10s %-12s %-14s %-8s %-10s\n",
           "Noeud", "Hash share", "Mines", "Chaine ref.", "Reward share", "Reorgs", "Max reorg");
    printf("--------------------------------------------------------------------------------\n");
    for (int i = 0; i < n_nodes; i++)
        printf("N%-5d %5.1f%%       %-10d %-12d %5.1f%%         %-8d %-10d\n", i, nodes[i].share * 100,
               r.mined_by[i], r.main_by[i], 100.0 * r.main_by[i] / r.main_len,
               r.reorgs[i], nodes[i].max_reorg);
    printf("\nBlocs mines          : %d\n", r.mined);
    printf("Longueur chaine ref. : %d\n", r.main_len);
    printf("Blocs orphelins      : %d (taux = %.2f%%)\n", r.orphans, r.orphan_rate * 100);
    printf("Propagation moyenne  : %.2f s (delai pour atteindre TOUS les noeuds)\n", r.avg_prop);
    printf("Noeuds d'accord      : %d / %d sur la chaine de reference\n", r.agree, n_nodes);

    int bad = verify_chain(nodes[0].tip);
    printf("Verification hash    : %s\n", bad == -1 ? "chaine integre" : "ERREUR");

    /* falsification d'un bloc au milieu de la chaine */
    int mid = nodes[0].tip;
    for (int i = 0; i < r.main_len / 2; i++) mid = blocks[mid].parent;
    double old_time = blocks[mid].time;
    blocks[mid].time += 5.0;
    bad = verify_chain(nodes[0].tip);
    printf("Falsification bloc %d (hauteur %d) -> %s\n", mid, blocks[mid].height,
           bad == mid ? "DETECTEE sur ce bloc" : "non detectee !");
    blocks[mid].time = old_time;

    /* ---------- Test 2 : influence de la latence ---------- */
    printf("\n=== Test 2 : influence de la latence (moyenne sur 10 simulations) ===\n\n");
    printf("%-12s %-14s %-16s %-14s\n", "Latence (s)", "Orphelins (%)", "Propagation (s)", "Reorg max");
    printf("------------------------------------------------------------\n");
    FILE *f = fopen("latency_sweep.csv", "w");
    if (f) fprintf(f, "latency,orphan_rate,avg_prop,max_reorg\n");
    double lat[] = {0, 0.5, 1, 2, 5, 10};
    for (int li = 0; li < 6; li++) {
        c.latency = lat[li];
        double so = 0, sp = 0; int mr = 0;
        for (int s = 1; s <= 10; s++) {
            sim_run(&c, s, &r);
            so += r.orphan_rate; sp += r.avg_prop;
            if (r.max_reorg > mr) mr = r.max_reorg;
        }
        printf("%-12.1f %-14.2f %-16.2f %-14d\n", lat[li], so / 10 * 100, sp / 10, mr);
        if (f) fprintf(f, "%.1f,%.4f,%.3f,%d\n", lat[li], so / 10, sp / 10, mr);
    }
    if (f) { fclose(f); printf("\n(resultats ecrits dans latency_sweep.csv)\n"); }
    return 0;
}