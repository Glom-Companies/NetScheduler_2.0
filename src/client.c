// src/client.c

#define _POSIX_C_SOURCE 200809L

#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "utils.h"
#include "log.h"

#define DEFAULT_SERVER_PORT 5000

static bool is_connected = false;
static int server_fd = -1;
static int user_priority = 2;

// ------------------------------------------------------------------------------------------------
// Définition du type TransferArg (à placer tout en haut) :
//   Ce struct est utilisé par les deux threads (send et recv) pour savoir quel fichier ouvrir, 
//   quel socket utiliser, etc.
// ------------------------------------------------------------------------------------------------
typedef struct {
    int sockfd;         // descripteur du socket vers le serveur
    char *local_path;   // chemin complet du fichier local à lire (source)
    long total_size;    // taille totale du fichier source (en octets)
    char *output_path;  // chemin complet du fichier de sortie (où écrire le flux reçu)
} TransferArg;

// ------------------------------------------------------------------------------------------------
// Vérifie si un programme existe dans le PATH (via la commande “which <prog>”).
// Retourne true si “which <prog>” renvoie 0, sinon false.
// ------------------------------------------------------------------------------------------------
static bool prog_exists(const char *prog) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "which %s > /dev/null 2>&1", prog);
    int r = system(cmd);
    return (r == 0);
}

// ------------------------------------------------------------------------------------------------
// Installe automatiquement (via apt-get) les dépendances nécessaires côté client :
//   - ncurses (libncurses5-dev), 
//   - ffmpeg,
//   - zstd (libzstd-dev).
// On affiche à l’écran ce qu’on fait et on demande le mot de passe sudo si besoin.
// ------------------------------------------------------------------------------------------------
static void install_dependencies_if_needed() {
    bool have_nc = prog_exists("tput");     // “tput” existe si ncurses est présent
    bool have_ffmpeg = prog_exists("ffmpeg");
    bool have_zstd = prog_exists("zstd");

    if (have_nc && have_ffmpeg && have_zstd) {
        // Tout est déjà installé
        return;
    }

    // On sort de ncurses (au cas où il était déjà initialisé)
    endwin();
    printf("\n===== Installation automatique des dépendances =====\n");

    if (!have_nc) {
        printf("ncurses manquant. Installation...\n");
        fflush(stdout);
        system("sudo apt-get update");
        system("sudo apt-get install -y libncurses5 libncurses5-dev libncursesw5-dev");
    }

    if (!have_ffmpeg) {
        printf("ffmpeg manquant. Installation...\n");
        fflush(stdout);
        system("sudo apt-get install -y ffmpeg");
    }

    if (!have_zstd) {
        printf("zstd manquant. Installation...\n");
        fflush(stdout);
        system("sudo apt-get install -y zstd libzstd-dev");
    }

    printf("Dépendances installées.\n");
    printf("Appuyez sur Entrée pour continuer...\n");
    getchar(); // Pause pour que l'utilisateur lise le message

    // Reprendre ncurses après l'installation
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
}

// ------------------------------------------------------------------------------------------------
// Tente de se connecter au serveur à l’adresse IP donnée (argv[1]).
// Retourne true si la connexion (socket + connect()) réussit, false sinon.
// Affiche un message à l'écran via ncurses pour indiquer la progression.
// ------------------------------------------------------------------------------------------------
bool connect_to_server(const char *server_ip) {
    struct sockaddr_in serv;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        return false;
    }
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(DEFAULT_SERVER_PORT);
    if (inet_pton(AF_INET, server_ip, &serv.sin_addr) <= 0) {
        close(server_fd);
        return false;
    }

    // Affichage ncurses : “Connexion en cours…”
    mvprintw(10, 4, "Connexion au serveur %s:%d...", server_ip, DEFAULT_SERVER_PORT);
    refresh();

    if (connect(server_fd, (struct sockaddr*)&serv, sizeof(serv)) < 0) {
        return false;
    }

    return true;
}

// Ferme la connexion proprement (envoie “BYE\n” puis close(fd))
void disconnect_from_server() {
    if (server_fd >= 0) {
        write_n_bytes(server_fd, "BYE\n", 4);
        close(server_fd);
        server_fd = -1;
    }
}

// ------------------------------------------------------------------------------------------------
// Thread qui lit le fichier local (local_path) par blocs et envoie chaque bloc au serveur via le socket.
//   - T : TransferArg* arg, contient sockfd, local_path, total_size...
// ------------------------------------------------------------------------------------------------
void *thread_send_func(void *arg) {
    TransferArg *t = (TransferArg*)arg;
    FILE *f = fopen(t->local_path, "rb");
    if (!f) {
        return NULL;
    }
    size_t block = 16 * 1024;
    void *buf = malloc(block);
    long rem = t->total_size;
    while (rem > 0) {
        size_t chunk = (rem < (long)block) ? rem : block;
        size_t r = fread(buf, 1, chunk, f);
        if (!r) break;
        write_n_bytes(t->sockfd, buf, r);
        rem -= r;
    }
    free(buf);
    fclose(f);
    return NULL;
}

// ------------------------------------------------------------------------------------------------
// Thread qui lit en boucle (non-bloquant si possible) la réponse du serveur (octets compressés / convertis)
//   et écrit ces octets dans le fichier de sortie (output_path).
// ------------------------------------------------------------------------------------------------
void *thread_recv_func(void *arg) {
    TransferArg *t = (TransferArg*)arg;
    FILE *f = fopen(t->output_path, "wb");
    if (!f) {
        return NULL;
    }
    char buf[8192];
    ssize_t r;
    while ((r = read(t->sockfd, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, r, f);
    }
    fclose(f);
    return NULL;
}

// ------------------------------------------------------------------------------------------------
// UI ncurses pour « Compresser un fichier »
//   - Demande le chemin du fichier/dossier,
//   - Envoie la commande "TASK|COMPRESS|<taille>|<chemin>|\n" au serveur,
//   - Lance deux threads : un pour envoyer (thread_send_func), un pour recevoir (thread_recv_func).
// ------------------------------------------------------------------------------------------------
void compress_file_ui() {
    echo();
    char path[256];
    mvprintw(10,4,"Chemin du fichier ou dossier à compresser : ");
    clrtoeol();
    getnstr(path,256);

    struct stat st;
    if (stat(path,&st) < 0) {
        mvprintw(12,4,"Erreur : introuvable ou illisible");
        getch();
        noecho();
        return;
    }
    long sz = st.st_size;
    noecho();

    mvprintw(12,4,"Envoi de la tâche de compression...");
    refresh();

    // Préparer la ligne réseau : TASK|COMPRESS|<taille>|<chemin>|
    char line[512];
    snprintf(line, sizeof(line),
             "TASK|COMPRESS|%ld|%s|\n", sz, path);
    write_n_bytes(server_fd, line, strlen(line));

    mvprintw(13,4,"Compression en cours, patientez...");
    refresh();

    TransferArg *arg = malloc(sizeof(*arg));
    arg->sockfd = server_fd;
    arg->local_path = strdup(path);
    arg->total_size = sz;
    char out[512];
    snprintf(out, sizeof(out), "%s.zst", path);
    arg->output_path = strdup(out);

    pthread_t stid, rtid;
    pthread_create(&stid, NULL, thread_send_func, arg);
    pthread_create(&rtid, NULL, thread_recv_func, arg);
    pthread_join(stid, NULL);
    pthread_join(rtid, NULL);

    mvprintw(15,4,"Compression terminée → %s", out);
    getch();

    free(arg->local_path);
    free(arg->output_path);
    free(arg);
}

// ------------------------------------------------------------------------------------------------
// UI ncurses pour « Convertir une vidéo »
//   - Demande le chemin du fichier vidéo,
//   - Demande le nom du fichier audio de sortie,
//   - Envoie "TASK|CONVERT|<taille>|<chemin>|<nom_sortie>\n" au serveur,
//   - Lance deux threads pour envoyer/réceptionner.
// ------------------------------------------------------------------------------------------------
void convert_video_ui() {
    echo();
    char path[256], outn[128];
    mvprintw(10,4,"Chemin de la vidéo à convertir : ");
    clrtoeol();
    getnstr(path,256);

    struct stat st;
    if (stat(path,&st) < 0) {
        mvprintw(12,4,"Erreur : introuvable ou illisible");
        getch();
        noecho();
        return;
    }
    long sz = st.st_size;

    mvprintw(11,4,"Nom du fichier audio de sortie (ex: sortie.mp3) : ");
    clrtoeol();
    getnstr(outn,128);
    noecho();

    mvprintw(13,4,"Envoi de la tâche de conversion...");
    refresh();

    // Préparer la ligne réseau : TASK|CONVERT|<taille>|<chemin>|<nom_sortie>\n
    char line[512];
    snprintf(line, sizeof(line),
             "TASK|CONVERT|%ld|%s|%s\n", sz, path, outn);
    write_n_bytes(server_fd, line, strlen(line));

    mvprintw(14,4,"Conversion en cours, patientez...");
    refresh();

    TransferArg *arg = malloc(sizeof(*arg));
    arg->sockfd = server_fd;
    arg->local_path = strdup(path);
    arg->total_size = sz;
    arg->output_path = strdup(outn);

    pthread_t stid, rtid;
    pthread_create(&stid, NULL, thread_send_func, arg);
    pthread_create(&rtid, NULL, thread_recv_func, arg);
    pthread_join(stid, NULL);
    pthread_join(rtid, NULL);

    mvprintw(16,4,"Conversion terminée → %s", outn);
    getch();

    free(arg->local_path);
    free(arg->output_path);
    free(arg);
}

int main(int argc, char *argv[]) {
    // 0) Vérification de l’argument : usage = ./scheduler_client <ip_serveur>
    if (argc < 2) {
        fprintf(stderr, "Usage : %s <ip_serveur>\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char *server_ip = argv[1];

    // 1) Avant d'initialiser ncurses, on installe automatiquement les dépendances si besoin.
    install_dependencies_if_needed();

    // 2) Initialisation ncurses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    // 3) Tentative de connexion
    if (!connect_to_server(server_ip)) {
        endwin();
        fprintf(stderr, "Erreur : impossible de se connecter à %s:%d\n",
                server_ip, DEFAULT_SERVER_PORT);
        return EXIT_FAILURE;
    }

    // 4) Affichage d’un message de connexion réussie + auth
    mvprintw(12, 4, "Connecté à %s:%d", server_ip, DEFAULT_SERVER_PORT);
    mvprintw(13, 4, "Envoi de l'authentification...");
    refresh();

    // 5) Saisie du pseudo pour l’authentification
    echo();
    char pseudo[64];
    mvprintw(15, 4, "Pseudo (uniquement lettres/chiffres) : ");
    clrtoeol();
    getnstr(pseudo, 64);
    noecho();

    // Envoi de "AUTH|<pseudo>\n"
    char auth_msg[128];
    snprintf(auth_msg, sizeof(auth_msg), "AUTH|%s\n", pseudo);
    write_n_bytes(server_fd, auth_msg, strlen(auth_msg));

    // Lecture de la réponse du serveur
    char resp[64];
    ssize_t r = read(server_fd, resp, sizeof(resp)-1);
    if (r <= 0) {
        endwin();
        fprintf(stderr, "Erreur : aucune réponse du serveur.\n");
        close(server_fd);
        return EXIT_FAILURE;
    }
    resp[r] = '\0';

    if (strncmp(resp, "AUTH_OK|", 8) == 0) {
        user_priority = atoi(resp + 8);
        mvprintw(17, 4, "Authentification réussie (prio = %d)", user_priority);
        refresh();
        sleep(1);
    } else {
        endwin();
        fprintf(stderr, "Échec de l'authentification : %s\n", resp);
        close(server_fd);
        return EXIT_FAILURE;
    }

    is_connected = true;

    // 6) Boucle principale : affichage du menu ncurses
    while (1) {
        clear();
        mvprintw(1, 2, "NetScheduler Client (prio = %d)", user_priority);
        mvprintw(3, 4, "1. Compresser un fichier");
        mvprintw(4, 4, "2. Convertir une vidéo");
        mvprintw(5, 4, "3. Se déconnecter");
        mvprintw(6, 4, "4. Quitter");
        refresh();

        int ch = getch();
        if (ch == '1') {
            compress_file_ui();
        }
        else if (ch == '2') {
            convert_video_ui();
        }
        else if (ch == '3') {
            // Se déconnecter et revenir à l'écran de connexion initial
            disconnect_from_server();
            is_connected = false;
            clear();
            mvprintw(3, 4, "Déconnecté. Appuyez sur une touche pour vous reconnecter.");
            refresh();
            getch();

            // Relancer le programme depuis le début (authentification à nouveau)
            endwin();
            execv(argv[0], argv);
            // Si execv échoue :
            fprintf(stderr, "Erreur interne : impossible de relancer le client.\n");
            return EXIT_FAILURE;
        }
        else if (ch == '4') {
            // Quitter définitivement
            if (is_connected) disconnect_from_server();
            endwin();
            return EXIT_SUCCESS;
        }
        else {
            // Touche non reconnue : on reste dans la boucle
            continue;
        }
    }

    // Ne devrait jamais être atteint
    endwin();
    return EXIT_SUCCESS;
}
