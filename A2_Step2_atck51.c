/* Atelier 2 - Etape 2 et 3 :
   transactions + soldes, puis attaque des 51% avec double depense.
   Le simulateur de reseau (noeuds, latence, forks) vient de l'etape 1. */
#include "common.h"
#include <math.h>

#define MAX_NODES  20
#define MAX_BLOCKS 6000
#define MAX_EVENTS 4000

/* ---- comptes et transactions du scenario ---- */
#define ACC_ATT      0        /* compte de l'attaquant (100 pieces au depart) */
#define ACC_MERCHANT 1        /* compte du commercant */
#define ACC_ATT2     2        /* 2e compte de l'attaquant (pour la double depense) */
#define COINS        100
#define NO_TX        0
#define T1_ID        1        /* T1 : attaquant -> commercant (100) */
#define T2_ID        2        /* T2 : attaquant -> lui-meme (100)   */
#define MAX_DEFICIT  15       /* l'attaquant abandonne s'il a 15 blocs de retard */

/* ================= 1. Generateur aleatoire (voir etape 1) ================= */
static unsigned long long rng_state = 1;
double rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (rng_state >> 11) / 9007199254740992.0;
}
void rng_seed(unsigned long long s) { rng_state = s ? s : 1; for (int i = 0; i < 10; i++) rnd(); }
double exp_sample(double mean) { return -mean * log(1.0 - rnd()); }

/* ================= 2. Structures de donnees ================= */
typedef struct {
    int    id, parent, height, miner;
    double time;
    int    tx_id, tx_from, tx_to, tx_amount;   /* 1 transaction maximum par bloc */
    int    valid;                              /* 0 si la transaction depense plus que le solde */
    char   hash[HASH_LEN];
    double last_received;
} Block;

typedef struct {
    double share;
    int    tip;
    char   known[MAX_BLOCKS];
    int    reorgs, max_reorg;
} Node;

enum { EV_MINE, EV_RECEIVE };
typedef struct { double time; int type, node, block; } Event;

typedef struct {
    int    n_nodes;                 /* noeuds HONNETES */
    double shares[MAX_NODES];       /* parts de hash des noeuds honnetes (%) */
    double block_time, latency;
    int    target_blocks;
    int    attack;                  /* 0 = reseau honnete, 1 = attaque 51% */
    double attacker_share;          /* part de hash de l'attaquant (%) */
    int    confirmations;           /* confirmations exigees par le commercant */
    int    verbose;                 /* 1 = afficher le deroulement */
} Config;

typedef struct {
    int mined, main_len, orphans, att_blocks;
    int shipped, published, success, gave_up;
    double ship_time;
} Result;

static Block blocks[MAX_BLOCKS];  static int block_count;
static Node  nodes[MAX_NODES];    static int n_nodes;
static Event queue[MAX_EVENTS];   static int qlen;

/* etat de l'attaque */
static int g_att_tip, g_shipped, g_published, g_gave_up, g_stop, g_verbose;

/* ================= 3. File d'evenements ================= */
void push_event(double t, int type, int node, int block) {
    queue[qlen].time = t; queue[qlen].type = type;
    queue[qlen].node = node; queue[qlen].block = block; qlen++;
}
int pop_event(Event *e) {
    if (qlen == 0) return 0;
    int best = 0;
    for (int i = 1; i < qlen; i++) if (queue[i].time < queue[best].time) best = i;
    *e = queue[best];
    queue[best] = queue[--qlen];
    return 1;
}

/* ================= 4. Soldes et transactions (NOUVEAU) ================= */
/* Solde d'un compte sur la chaine qui se termine au bloc 'tip' */
int balance(int tip, int acc) {
    int bal = (acc == ACC_ATT) ? COINS : 0;          /* la genese donne 100 pieces a l'attaquant */
    for (int x = tip; x != -1; x = blocks[x].parent) {
        Block *b = &blocks[x];
        if (b->tx_id != NO_TX) {
            if (b->tx_from == acc) bal -= b->tx_amount;
            if (b->tx_to   == acc) bal += b->tx_amount;
        }
    }
    return bal;
}

/* Bloc de la chaine (jusqu'a tip) qui contient la transaction tx_id, ou -1 */
int chain_tx_block(int tip, int tx_id) {
    for (int x = tip; x != -1; x = blocks[x].parent)
        if (blocks[x].tx_id == tx_id) return x;
    return -1;
}

/* Nombre de confirmations de la transaction (1 = elle vient d'etre incluse) */
int confirmations(int tip, int tx_id) {
    int b = chain_tx_block(tip, tx_id);
    return b < 0 ? 0 : blocks[tip].height - blocks[b].height + 1;
}

/* ================= 5. Blocs ================= */
void compute_block_hash(int id, char out[HASH_LEN]) {
    char buf[200];
    Block *b = &blocks[id];
    sprintf(buf, "%.16s|%d|%d|%.3f|%d:%d>%d:%d", blocks[b->parent].hash, b->height, b->miner,
            b->time, b->tx_id, b->tx_from, b->tx_to, b->tx_amount);
    hash_to_hex(buf, out);
}

int new_block(int parent, int miner, double time, int tx_id, int from, int to, int amount) {
    Block *b = &blocks[block_count];
    b->id = block_count; b->parent = parent; b->miner = miner; b->time = time;
    b->height = blocks[parent].height + 1;
    b->tx_id = tx_id; b->tx_from = from; b->tx_to = to; b->tx_amount = amount;
    b->valid = 1;
    if (tx_id != NO_TX && balance(parent, from) < amount) b->valid = 0;  /* depense impossible */
    b->last_received = time;
    compute_block_hash(b->id, b->hash);
    return block_count++;
}

/* ================= 6. Reception d'un bloc ================= */
void node_accept(int n, int b, double now) {
    Node *nd = &nodes[n];
    if (!blocks[b].valid) {                      /* un bloc invalide est rejete */
        if (g_verbose) printf("  [t=%6.1f] noeud %d REJETTE le bloc %d : transaction invalide\n", now, n, b);
        return;
    }
    if (nd->known[b]) return;
    for (int x = b; x != -1 && !nd->known[x]; x = blocks[x].parent) {
        nd->known[x] = 1;
        if (now > blocks[x].last_received) blocks[x].last_received = now;
    }
    if (blocks[b].height > blocks[nd->tip].height) {
        int x = nd->tip, y = b;
        while (blocks[x].height > blocks[y].height) x = blocks[x].parent;
        while (blocks[y].height > blocks[x].height) y = blocks[y].parent;
        while (x != y) { x = blocks[x].parent; y = blocks[y].parent; }
        int depth = blocks[nd->tip].height - blocks[x].height;
        if (depth > 0) {
            nd->reorgs++;
            if (depth > nd->max_reorg) nd->max_reorg = depth;
            if (g_verbose && n == 0)
                printf("  [t=%6.1f] COMMERCANT : reorganisation, %d bloc(s) abandonne(s)\n", now, depth);
        }
        nd->tip = b;
    }
}

/* ================= 7. Simulation ================= */
void world_init(Config *c) {
    block_count = 0; qlen = 0; n_nodes = c->n_nodes;
    Block *g = &blocks[0];
    memset(g, 0, sizeof(Block));
    g->id = 0; g->parent = -1; g->height = 0; g->miner = -1; g->valid = 1;
    hash_to_hex("genesis", g->hash);
    block_count = 1;

    double total = c->attacker_share;
    for (int i = 0; i < n_nodes; i++) total += c->shares[i];
    for (int i = 0; i < n_nodes; i++) {
        memset(&nodes[i], 0, sizeof(Node));
        nodes[i].share = c->shares[i] / total;
        nodes[i].known[0] = 1;
    }
}

void broadcast(Config *c, int b, int from_node, double t) {
    for (int k = 0; k < n_nodes; k++) {
        if (k == from_node) continue;
        double delay = c->latency * (0.5 + rnd());
        push_event(t + delay, EV_RECEIVE, k, b);
    }
}

int public_height(void) {                         /* hauteur de la plus longue chaine honnete */
    int h = 0;
    for (int i = 0; i < n_nodes; i++)
        if (blocks[nodes[i].tip].height > h) h = blocks[nodes[i].tip].height;
    return h;
}

void sim_run(Config *c, unsigned long long seed, Result *r) {
    rng_seed(seed);
    world_init(c);
    g_att_tip = 0; g_shipped = g_published = g_gave_up = g_stop = 0;
    g_verbose = c->verbose;
    memset(r, 0, sizeof(Result));

    double total = c->attacker_share;
    for (int i = 0; i < n_nodes; i++) total += c->shares[i];
    double att_frac = c->attacker_share / total;

    push_event(exp_sample(c->block_time), EV_MINE, -1, -1);
    Event e;
    while (pop_event(&e)) {
        if (e.type == EV_MINE) {
            if (g_stop) continue;
            double roll = rnd();
            if (c->attack && roll < att_frac) {
                /* ----- l'attaquant mine EN SECRET sur sa chaine privee ----- */
                int tx = NO_TX;
                if (chain_tx_block(g_att_tip, T2_ID) == -1) tx = T2_ID;   /* T2 dans son 1er bloc */
                int b = new_block(g_att_tip, n_nodes, e.time, tx,
                                  ACC_ATT, ACC_ATT2, tx ? COINS : 0);
                g_att_tip = b;
                r->mined++; r->att_blocks++;
                if (g_verbose)
                    printf("  [t=%6.1f] attaquant : bloc prive (hauteur %d)%s | chaine publique : %d\n",
                           e.time, blocks[b].height, tx ? " contient T2" : "", public_height());
            } else {
                /* ----- un noeud honnete mine sur sa chaine publique ----- */
                if (c->attack) roll = (roll - att_frac) / (1.0 - att_frac);
                double sum = 0; int m = n_nodes - 1;
                for (int i = 0; i < n_nodes; i++) {
                    sum += nodes[i].share / (1.0 - (c->attack ? att_frac : 0));
                    if (roll < sum) { m = i; break; }
                }
                int tx = NO_TX, amt = 0;
                if (c->attack && chain_tx_block(nodes[m].tip, T1_ID) == -1 &&
                    balance(nodes[m].tip, ACC_ATT) >= COINS) { tx = T1_ID; amt = COINS; }
                int b = new_block(nodes[m].tip, m, e.time, tx, ACC_ATT, ACC_MERCHANT, amt);
                r->mined++;
                if (g_verbose && tx)
                    printf("  [t=%6.1f] noeud %d inclut T1 (attaquant -> commercant) dans le bloc de hauteur %d\n",
                           e.time, m, blocks[b].height);
                node_accept(m, b, e.time);
                broadcast(c, b, m, e.time);
            }
            if (r->mined < c->target_blocks && !g_stop)
                push_event(e.time + exp_sample(c->block_time), EV_MINE, -1, -1);
        } else {
            node_accept(e.node, e.block, e.time);
        }

        if (!c->attack) continue;

        /* ---- le commercant livre apres 'confirmations' confirmations ---- */
        if (!g_shipped && confirmations(nodes[0].tip, T1_ID) >= c->confirmations) {
            g_shipped = 1; r->shipped = 1; r->ship_time = e.time;
            if (g_verbose)
                printf("  [t=%6.1f] COMMERCANT : %d confirmation(s) -> il LIVRE la marchandise\n",
                       e.time, c->confirmations);
        }
        /* ---- l'attaquant decide : publier ou abandonner ---- */
        if (g_shipped && !g_published && !g_gave_up) {
            int ph = public_height(), ah = blocks[g_att_tip].height;
            if (ah > ph) {
                g_published = 1; g_stop = 1; r->published = 1;
                if (g_verbose)
                    printf("  [t=%6.1f] attaquant : chaine privee (%d) plus longue que la publique (%d) -> PUBLIE\n",
                           e.time, ah, ph);
                broadcast(c, g_att_tip, -1, e.time);
            } else if (ph - ah >= MAX_DEFICIT) {
                g_gave_up = 1; g_stop = 1; r->gave_up = 1;
                if (g_verbose) printf("  [t=%6.1f] attaquant : trop de retard (%d blocs) -> ABANDONNE\n", e.time, ph - ah);
            }
        }
    }

    /* ---- resultats ---- */
    int ref = nodes[0].tip;
    r->main_len = blocks[ref].height;
    r->orphans = r->mined - r->main_len;
    r->success = c->attack && g_published && chain_tx_block(ref, T1_ID) == -1;
}

/* ================= 8. Theorie (Rosenfeld) pour valider le simulateur ================= */
/* Probabilite qu'un attaquant de part q rattrape (et depasse) la chaine publique
   apres z confirmations. */
double theory(double q, int z) {
    if (q >= 0.5) return 1.0;
    double p = 1.0 - q, lambda = z * q / p, poisson = exp(-lambda), sum = 0;
    for (int k = 0; k <= z; k++) {
        if (k > 0) poisson *= lambda / k;
        sum += poisson * (1.0 - pow(q / p, z - k + 1));
    }
    return 1.0 - sum;
}

/* ================= 9. Tests ================= */
void set_config(Config *c, double q, int z) {
    memset(c, 0, sizeof(Config));
    c->n_nodes = 5;
    for (int i = 0; i < 5; i++) c->shares[i] = (100.0 - q) / 5.0;   /* honnetes : le reste du hash */
    c->block_time = 10.0; c->latency = 1.0;
    c->target_blocks = 3000;
    c->attack = 1; c->attacker_share = q; c->confirmations = z;
}

int main(void) {
    Config c; Result r;

    /* ---------- Test 0 : double depense impossible sur UNE seule chaine ---------- */
    printf("=== Test 0 : on ne peut pas depenser deux fois sur la meme chaine ===\n\n");
    set_config(&c, 0, 1); c.attack = 0; c.verbose = 1;
    world_init(&c); g_verbose = 1;
    int b1 = new_block(0, 0, 1.0, T1_ID, ACC_ATT, ACC_MERCHANT, COINS);
    node_accept(0, b1, 1.0);
    printf("  Bloc 1 : T1 (attaquant -> commercant, 100)  -> valide = %d\n", blocks[b1].valid);
    printf("  Soldes sur la chaine : attaquant = %d, commercant = %d\n",
           balance(nodes[0].tip, ACC_ATT), balance(nodes[0].tip, ACC_MERCHANT));
    int b2 = new_block(b1, 0, 2.0, T2_ID, ACC_ATT, ACC_ATT2, COINS);
    printf("  Bloc 2 : T2 (attaquant -> lui-meme, 100)    -> valide = %d (solde insuffisant)\n", blocks[b2].valid);
    node_accept(0, b2, 2.0);
    printf("  Tete de chaine du noeud : bloc %d (inchangee)\n\n", nodes[0].tip);
    printf("  => Pour double-depenser, l'attaquant doit miner une chaine PARALLELE et plus longue.\n");

    /* ---------- Test 1 : une attaque racontee ---------- */
    printf("\n=== Test 1a : attaque reussie (attaquant 40%%, commercant attend 3 confirmations) ===\n\n");
    set_config(&c, 40, 3); c.verbose = 1;
    sim_run(&c, 20, &r);
    printf("\n  Resultat : %s\n", r.success ? "DOUBLE DEPENSE REUSSIE" : "attaque echouee");
    printf("  Solde du commercant sur sa chaine finale : %d (avant l'attaque : %d)\n",
           balance(nodes[0].tip, ACC_MERCHANT), COINS);
    printf("  Solde compte 2 de l'attaquant : %d | solde compte 1 : %d\n",
           balance(nodes[0].tip, ACC_ATT2), balance(nodes[0].tip, ACC_ATT));
    if (r.success) printf("  => le commercant a livre la marchandise et n'a PAS ete paye.\n");

    printf("\n=== Test 1b : attaque echouee (attaquant 10%%, commercant attend 3 confirmations) ===\n\n");
    set_config(&c, 10, 3); c.verbose = 1;
    sim_run(&c, 5, &r);
    printf("\n  Resultat : %s\n", r.success ? "DOUBLE DEPENSE REUSSIE" : "attaque echouee");
    printf("  Solde du commercant : %d (il a bien ete paye)\n", balance(nodes[0].tip, ACC_MERCHANT));

    /* ---------- Test 2 : taux de reussite selon la puissance et les confirmations ---------- */
    int RUNS = 300;
    double qs[]  = {10, 20, 30, 40, 45, 51, 60};
    int    zs[]  = {1, 2, 3, 6, 10};
    printf("\n=== Test 2 : taux de reussite de l'attaque (%d simulations par case) ===\n", RUNS);
    printf("Format : simule %% (theorie %%)\n\n");
    printf("%-14s", "Attaquant");
    for (int zi = 0; zi < 5; zi++) printf("z=%-12d", zs[zi]);
    printf("\n--------------------------------------------------------------------------\n");
    FILE *f = fopen("attack51_results.csv", "w");
    if (f) fprintf(f, "attacker_share,confirmations,success_rate_sim,success_rate_theory\n");
    for (int qi = 0; qi < 7; qi++) {
        printf("%-13.0f%%", qs[qi]);
        for (int zi = 0; zi < 5; zi++) {
            int wins = 0;
            for (int s = 1; s <= RUNS; s++) {
                set_config(&c, qs[qi], zs[zi]);
                sim_run(&c, (unsigned long long)s * 7919ULL + qi * 31 + zi, &r);
                wins += r.success;
            }
            double sim = 100.0 * wins / RUNS, th = 100.0 * theory(qs[qi] / 100.0, zs[zi]);
            printf("%5.1f (%5.1f) ", sim, th);
            if (f) fprintf(f, "%.0f,%d,%.4f,%.4f\n", qs[qi], zs[zi], sim / 100, th / 100);
        }
        printf("\n");
    }
    if (f) { fclose(f); printf("\n(resultats ecrits dans attack51_results.csv)\n"); }
    return 0;
}