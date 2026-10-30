//HASHING FUNCTION/ DBJé algo developpé par daniel berstien 
#ifndef COMMON_H
#define COMMON_H
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
 
#define HASH_LEN 17   /* 16 caractères hexadécimaux + '\0' */
#define TX_STR   64   /* taille max d'une transaction sous forme de texte */
 
/* Algorithme DJB2 de Daniel J. Bernstein : hash = hash * 33 + c */
static inline unsigned long long djb2(const char *str) {
    unsigned long long hash = 5381;
    int c;
    while ((c = (unsigned char)*str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}
 
/* Hash d'une chaîne, écrit en hexadécimal (16 caractères) */
static inline void hash_to_hex(const char *s, char out[HASH_LEN]) {
    sprintf(out, "%016llx", djb2(s));
}
 
/* Temps courant en millisecondes */
static inline double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
 
/* Preuve de travail valide : le hash commence par 'diff' zéros */
static inline int valid_pow(const char *hash, int diff) {
    for (int i = 0; i < diff; i++)
        if (hash[i] != '0') return 0;
    return 1;
}
 
/* Racine de Merkle d'un tableau de chaînes (transactions) */
static inline void merkle_root(char data[][TX_STR], int n, char out[HASH_LEN]) {
    char level[64][HASH_LEN];
    int count = n;
    if (n == 0) { hash_to_hex("", out); return; }
    for (int i = 0; i < n; i++) hash_to_hex(data[i], level[i]);
    while (count > 1) {
        if (count % 2) {                       /* nombre impair : on duplique le dernier */
            strcpy(level[count], level[count - 1]);
            count++;
        }
        for (int i = 0; i < count; i += 2) {
            char buf[2 * HASH_LEN];
            sprintf(buf, "%.16s%.16s", level[i], level[i + 1]);
            hash_to_hex(buf, level[i / 2]);
        }
        count /= 2;
    }
    strcpy(out, level[0]);
}
 
#endif