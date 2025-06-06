#include "userauth.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Structure interne pour stocker un utilisateur chargé depuis users.txt
typedef struct {
    char *pseudo;
    int priority;
} User;

// Tableau dynamique d’utilisateurs
static User *users = NULL;
static int user_count = 0;

int load_users(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1;

    char line[256];
    // 1) Compter le nombre de lignes utiles
    user_count = 0;
    while (fgets(line, sizeof(line), f)) {
        // Ignorer commentaires et lignes vides
        if (line[0] == '#' || line[0] == '\n') continue;
        user_count++;
    }
    if (user_count <= 0) {
        fclose(f);
        return 0;
    }
    rewind(f);

    // Allouer le tableau
    users = malloc(sizeof(User) * user_count);
    if (!users) {
        fclose(f);
        return -1;
    }

    int idx = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        // Supprimer le '\n' final
        line[strcspn(line, "\n")] = '\0';
        // Découper en pseudo:priority
        char *p1 = strtok(line, ":");
        char *p2 = strtok(NULL, ":");
        if (!p1 || !p2) continue;
        users[idx].pseudo = strdup(p1);
        users[idx].priority = atoi(p2);
        idx++;
    }
    user_count = idx;
    fclose(f);
    return user_count;
}

bool find_user_priority(const char *pseudo, int *out_priority) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(pseudo, users[i].pseudo) == 0) {
            *out_priority = users[i].priority;
            return true;
        }
    }
    return false;
}
