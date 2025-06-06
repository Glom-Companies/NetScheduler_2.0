#ifndef USERAUTH_H
#define USERAUTH_H

#include <stdbool.h>

// Charge tous les utilisateurs depuis le fichier filename (ex : "users.txt").
// Retourne le nombre d’utilisateurs chargés, ou -1 en cas d’erreur (fichier introuvable, malloc échoué).
int load_users(const char *filename);

// Recherche le pseudo dans la liste chargée.
// Si trouvé, stocke la priorité dans *out_priority (0, 1, 2), retourne true.
// Sinon, retourne false.
bool find_user_priority(const char *pseudo, int *out_priority);

#endif // USERAUTH_H
