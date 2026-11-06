/* Atelier 2 - Etape 4 : minage egoiste (selfish mining)
   Strategie d'Eyal et Sirer sur le simulateur de reseau des etapes precedentes. */
#include "common.h"
#include <math.h>

#define MAX_NODES  20
#define MAX_BLOCKS 6000
#define MAX_EVENTS 4000

/* ================= 1. Generateur aleatoire ================= */
static unsigned long long rng_state = 1;
double rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (rng_state >> 11) / 9007199254740992.0;
}
void rng_seed(unsigned long long s) { rng_state = s ? s : 1; for (int i = 0; i < 10; i++) rnd(); }
double exp_sample(double mean) { return -mean * log(1.0 - rnd()); }

/* ================= 2. Structures ================= */
typedef struct {
    int id, parent, height, miner;
    double time;
    char hash[HASH_LEN];
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
    int    n_nodes;               /* le DERNIER noeud est l'attaquant */
    double shares[MAX_NODES];     /* parts de hash (%) */
    double block_time, latency;
    int    target_blocks;
    int    selfish;               /* 1 = minage egoiste, 0 = l'attaquant mine honnetement */
    double att_delay_factor;      /* delai de diffusion de l'attaquant (x latence) : 0 = tres bien connecte */
    int    verbose;
} Config;

typedef struct {
    int    mined, main_len, orphans, att_main, max_reorg;
    int    on_att, on_hon;        /* pour estimer gamma */
    double orphan_rate, att_share, gamma;
} Result;

static Block blocks[MAX_BLOCKS];  static int block_count;
static Node  nodes[MAX_NODES];    static int n_nodes, att_id;
static Event queue[MAX_EVENTS];   static int qlen;

/* etat du minage egoiste */
static int g_att_tip, g_pub_tip, g_att_pub_h, g_race, g_verbose;
static int g_on_att, g_on_hon;

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

/* ================= 4. Blocs ================= */
int new_block(int parent, int miner, double time) {
    Block *b = &blocks[block_count];
    char buf[200];
    b->id = block_count; b->parent = parent; b->miner = miner; b->time = time;
    b->height = blocks[parent].height + 1;
    sprintf(buf, "%.16s|%d|%d|%.3f", blocks[parent].hash, b->height, miner, time);
    hash_to_hex(buf, b->hash);
    return block_count++;
}
int height(int b) { return blocks[b].height; }

int block_at_height(int tip, int h) {            /* bloc de la chaine de 'tip' a la hauteur h */
    int x = tip;
    while (height(x) > h) x = blocks[x].parent;
    return x;
}

/* ================= 5. Reception d'un bloc par un noeud ================= */
void node_accept(int n, int b) {
    Node *nd = &nodes[n];
    if (nd->known[b]) return;
    for (int x = b; x != -1 && !nd->known[x]; x = blocks[x].parent) nd->known[x] = 1;
    if (height(b) > height(nd->tip)) {
        int x = nd->tip, y = b;
        while (height(x) > height(y)) x = blocks[x].parent;
        while (height(y) > height(x)) y = blocks[y].parent;
        while (x != y) { x = blocks[x].parent; y = blocks[y].parent; }
        int depth = height(nd->tip) - height(x);
        if (depth > 0) { nd->reorgs++; if (depth > nd->max_reorg) nd->max_reorg = depth; }
        nd->tip = b;
    }
}

/* ================= 6. Diffusion ================= */
void broadcast(Config *c, int b, int from_node, double t) {      /* diffusion honnete */
    for (int k = 0; k < n_nodes; k++) {
        if (k == from_node) continue;
        push_event(t + c->latency * (0.5 + rnd()), EV_RECEIVE, k, b);
    }
}
void publish(Config *c, int b, double t) {                       /* publication par l'attaquant */
    for (int k = 0; k < n_nodes; k++) {
        if (k == att_id) continue;
        push_event(t + c->latency * c->att_delay_factor * (0.5 + rnd()), EV_RECEIVE, k, b);
    }
    if (height(b) > g_att_pub_h) g_att_pub_h = height(b);
}

/* ================= 7. Strategie du minage egoiste ================= */
/* L'attaquant mine un bloc */
void selfish_on_own_block(Config *c, int b, double t) {
    int race = (height(g_att_tip) == height(g_pub_tip) && g_att_tip != g_pub_tip);
    g_att_tip = b;
    if (race) {                                   /* il gagne la course : il publie TOUT */
        if (g_verbose) printf("  [t=%6.1f] attaquant trouve un bloc pendant la COURSE -> publie 2 blocs, il gagne\n", t);
        publish(c, b, t);
        g_pub_tip = b;
        g_race = 0;
    } else {                                      /* sinon il GARDE le bloc secret */
        if (g_verbose) printf("  [t=%6.1f] attaquant trouve un bloc, le GARDE SECRET (avance = %d)\n",
                              t, height(g_att_tip) - height(g_pub_tip));
    }
}

/* Un noeud honnete vient de trouver le bloc b (l'attaquant le sait tout de suite) */
void selfish_on_honest_block(Config *c, int b, double t) {
    int old_pub_h = height(g_pub_tip);
    if (height(b) <= old_pub_h) return;           /* bloc concurrent du meme niveau : ignore */
    if (g_race) {                                 /* la course se termine : sur quelle branche ? */
        if (blocks[b].parent == g_att_tip) g_on_att++; else g_on_hon++;
        g_race = 0;
    }
    int lead = height(g_att_tip) - old_pub_h;     /* avance de l'attaquant AVANT ce bloc */
    g_pub_tip = b;
    if (g_verbose) printf("  [t=%6.1f] un noeud honnete trouve un bloc | avance de l'attaquant = %d -> ", t, lead);

    if (lead <= 0) {                              /* rien a cacher : il rejoint la chaine publique */
        if (g_verbose) printf("il adopte la chaine publique\n");
        g_att_tip = b; g_att_pub_h = height(b);
    } else if (lead == 1) {                       /* avance 1 : il publie son bloc -> course */
        if (g_verbose) printf("il publie son bloc : COURSE (egalite)\n");
        publish(c, g_att_tip, t);
        g_race = 1;
    } else if (lead == 2) {                       /* avance 2 : il publie TOUT et gagne */
        if (g_verbose) printf("il publie ses 2 blocs : il GAGNE, le bloc honnete est orphelin\n");
        publish(c, g_att_tip, t);
        g_pub_tip = g_att_tip;
    } else {                                      /* grande avance : il publie 1 bloc, garde l'avance */
        if (g_verbose) printf("il publie 1 bloc et garde le reste (avance = %d)\n", lead - 1);
        publish(c, block_at_height(g_att_tip, height(b)), t);
    }
}

/* ================= 8. Simulation ================= */
void sim_run(Config *c, unsigned long long seed, Result *r) {
    rng_seed(seed);
    block_count = 0; qlen = 0; n_nodes = c->n_nodes; att_id = n_nodes - 1;
    g_verbose = c->verbose;

    Block *g = &blocks[0];
    memset(g, 0, sizeof(Block));
    g->parent = -1; g->miner = -1;
    hash_to_hex("genesis", g->hash);
    block_count = 1;

    double total = 0;
    for (int i = 0; i < n_nodes; i++) total += c->shares[i];
    for (int i = 0; i < n_nodes; i++) {
        memset(&nodes[i], 0, sizeof(Node));
        nodes[i].share = c->shares[i] / total;
        nodes[i].known[0] = 1;
    }
    g_att_tip = g_pub_tip = 0; g_att_pub_h = 0; g_race = 0; g_on_att = g_on_hon = 0;
    memset(r, 0, sizeof(Result));

    push_event(exp_sample(c->block_time), EV_MINE, -1, -1);
    Event e;
    while (pop_event(&e)) {
        if (e.type == EV_MINE) {
            double roll = rnd(), sum = 0;
            int m = n_nodes - 1;
            for (int i = 0; i < n_nodes; i++) { sum += nodes[i].share; if (roll < sum) { m = i; break; } }

            if (c->selfish && m == att_id) {
                int b = new_block(g_att_tip, att_id, e.time);
                r->mined++;
                selfish_on_own_block(c, b, e.time);
            } else {
                int b = new_block(nodes[m].tip, m, e.time);
                r->mined++;
                node_accept(m, b);
                broadcast(c, b, m, e.time);
                if (c->selfish) selfish_on_honest_block(c, b, e.time);
            }
            if (r->mined < c->target_blocks)
                push_event(e.time + exp_sample(c->block_time), EV_MINE, -1, -1);
        } else {
            node_accept(e.node, e.block);
        }
    }

    /* ---- resultats : chaine de reference = celle du noeud 0 ---- */
    int ref = nodes[0].tip;
    r->main_len = height(ref);
    for (int x = ref; x != 0; x = blocks[x].parent) if (blocks[x].miner == att_id) r->att_main++;
    r->orphans = r->mined - r->main_len;
    r->orphan_rate = (double)r->orphans / r->mined;
    r->att_share = (double)r->att_main / r->main_len;
    for (int i = 0; i < n_nodes - 1; i++) if (nodes[i].max_reorg > r->max_reorg) r->max_reorg = nodes[i].max_reorg;
    r->on_att = g_on_att; r->on_hon = g_on_hon;
}

/* ================= 9. Theorie (Eyal et Sirer, 2014) ================= */
/* Part de recompense du mineur egoiste (part de hash q, gamma = part du reseau honnete
   qui mine sur son bloc en cas d'egalite) */
double theory_share(double q, double gamma) {
    double num = q * (1 - q) * (1 - q) * (4 * q + gamma * (1 - 2 * q)) - q * q * q;
    double den = 1 - q * (1 + (2 - q) * q);
    return num / den;
}
/* Seuil de rentabilite : au-dessus, le minage egoiste rapporte plus que le minage honnete */
double theory_threshold(double gamma) { return (1 - gamma) / (3 - 2 * gamma); }

/* ================= 10. Tests ================= */
void set_config(Config *c, double q, int selfish, double factor) {
    memset(c, 0, sizeof(Config));
    c->n_nodes = 6;
    for (int i = 0; i < 5; i++) c->shares[i] = (100.0 - q) / 5.0;   /* 5 noeuds honnetes */
    c->shares[5] = q;                                                /* l'attaquant */
    c->block_time = 10.0; c->latency = 1.0;
    c->target_blocks = 2000;
    c->selfish = selfish; c->att_delay_factor = factor;
}

/* Moyenne sur 'runs' simulations */
void run_avg(double q, int selfish, double factor, int runs, Result *avg) {
    Config c; Result r;
    double sh = 0, orph = 0; int on_a = 0, on_h = 0, mr = 0;
    for (int s = 1; s <= runs; s++) {
        set_config(&c, q, selfish, factor);
        sim_run(&c, (unsigned long long)s * 7919ULL + (unsigned long long)(q * 13) + selfish, &r);
        sh += r.att_share; orph += r.orphan_rate; on_a += r.on_att; on_h += r.on_hon;
        if (r.max_reorg > mr) mr = r.max_reorg;
    }
    avg->att_share = sh / runs; avg->orphan_rate = orph / runs; avg->max_reorg = mr;
    avg->gamma = (on_a + on_h) ? (double)on_a / (on_a + on_h) : 0;
}

int main(void) {
    Config c; Result r;
    int RUNS = 20;
    double qs[] = {10, 15, 20, 25, 30, 35, 40, 45};

    /* ---------- Test 1 : une execution racontee ---------- */
    printf("=== Test 1 : minage egoiste raconte (attaquant 35%%, 22 blocs) ===\n\n");
    set_config(&c, 35, 1, 1.0); c.target_blocks = 22; c.verbose = 1;
    sim_run(&c, 4, &r);
    printf("\n  Blocs mines : %d | dans la chaine : %d | orphelins : %d\n", r.mined, r.main_len, r.orphans);
    printf("  Blocs de l'attaquant dans la chaine : %d sur %d\n", r.att_main, r.main_len);

    /* ---------- Test 2 : honnete vs egoiste, et validation par la theorie ---------- */
    printf("\n=== Test 2 : l'attaquant gagne-t-il plus en trichant ? (%d simulations de 2000 blocs) ===\n\n", RUNS);
    printf("%-9s %-14s %-14s %-9s %-14s\n", "Part de", "Strategie", "Strategie", "gamma", "Theorie");
    printf("%-9s %-14s %-14s %-9s %-14s\n", "hash", "honnete", "egoiste", "mesure", "(gamma mesure)");
    printf("-------------------------------------------------------------------\n");
    FILE *f = fopen("selfish_results.csv", "w");
    if (f) fprintf(f, "attacker_share,delay_factor,honest_revenue,selfish_revenue,gamma,theory,orphan_honest,orphan_selfish\n");
    for (int qi = 0; qi < 8; qi++) {
        Result h, s;
        run_avg(qs[qi], 0, 1.0, RUNS, &h);
        run_avg(qs[qi], 1, 1.0, RUNS, &s);
        double th = theory_share(qs[qi] / 100.0, s.gamma);
        printf("%-8.0f%% %-13.1f%% %-13.1f%% %-9.2f %-13.1f%% %s\n", qs[qi],
               h.att_share * 100, s.att_share * 100, s.gamma, th * 100,
               s.att_share > h.att_share ? " <- tricher rapporte plus" : "");
        if (f) fprintf(f, "%.0f,1.0,%.4f,%.4f,%.3f,%.4f,%.4f,%.4f\n", qs[qi],
                       h.att_share, s.att_share, s.gamma, th, h.orphan_rate, s.orphan_rate);
    }

    /* ---------- Test 3 : influence de la connectivite de l'attaquant ---------- */
    printf("\n=== Test 3 : influence de la qualite de connexion de l'attaquant ===\n");
    printf("Part de recompense de l'attaquant egoiste (gamma mesure entre parentheses)\n\n");
    double factors[] = {0.0, 1.0, 5.0};
    printf("%-9s %-18s %-18s %-18s\n", "Part de", "Tres connecte", "Normal", "Mal connecte");
    printf("%-9s %-18s %-18s %-18s\n", "hash", "(delai x0)", "(delai x1)", "(delai x5)");
    printf("---------------------------------------------------------------\n");
    for (int qi = 0; qi < 8; qi++) {
        printf("%-8.0f%% ", qs[qi]);
        for (int fi = 0; fi < 3; fi++) {
            Result s;
            run_avg(qs[qi], 1, factors[fi], RUNS, &s);
            printf("%5.1f%% (g=%.2f)    ", s.att_share * 100, s.gamma);
            if (f && fi != 1) fprintf(f, "%.0f,%.1f,,%.4f,%.3f,%.4f,,\n", qs[qi], factors[fi],
                                      s.att_share, s.gamma, theory_share(qs[qi] / 100.0, s.gamma));
        }
        printf("\n");
    }
    printf("\nSeuil de rentabilite theorique (1-g)/(3-2g) : g=0 -> %.1f%% | g=0.5 -> %.1f%% | g=1 -> %.1f%%\n",
           theory_threshold(0) * 100, theory_threshold(0.5) * 100, theory_threshold(1) * 100);

    /* ---------- Test 4 : signature de l'attaque (pour le futur detecteur) ---------- */
    printf("\n=== Test 4 : indicateurs observables (attaquant 30%%) ===\n\n");
    Result h, s;
    run_avg(30, 0, 1.0, RUNS, &h);
    run_avg(30, 1, 1.0, RUNS, &s);
    printf("%-34s %-14s %-14s\n", "Indicateur", "Honnete", "Egoiste");
    printf("-----------------------------------------------------------\n");
    printf("%-34s %-13.1f%% %-13.1f%%\n", "Taux de blocs orphelins", h.orphan_rate * 100, s.orphan_rate * 100);
    printf("%-34s %-14d %-14d\n", "Plus grande reorganisation (blocs)", h.max_reorg, s.max_reorg);
    printf("%-34s %-13.1f%% %-13.1f%%\n", "Part de recompense de l'attaquant", h.att_share * 100, s.att_share * 100);
    printf("%-34s %-13.1f%% %-13.1f%%\n", "(sa part de puissance de hash)", 30.0, 30.0);

    if (f) { fclose(f); printf("\n(resultats ecrits dans selfish_results.csv)\n"); }
    return 0;
}