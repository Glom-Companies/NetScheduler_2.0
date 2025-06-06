# -------------------------------------------------------------------
CC            = gcc
CFLAGS        = -Wall -Wextra -std=c99 -O2 -D_POSIX_C_SOURCE=200809L -pthread
LDFLAGS_SERVER = -lncurses -lzstd   # Remarque : -lzstd (pas -lz) pour zstd
LDFLAGS_CLIENT = -lncurses

SRC_DIR       = src

# -------------------------------------------------------------------
# Objets pour le serveur (liste explicite des .o)
OBJ_SERVER = \
    $(SRC_DIR)/server.o \
    $(SRC_DIR)/netqueue.o \
    $(SRC_DIR)/userauth.o \
    $(SRC_DIR)/utils.o \
    $(SRC_DIR)/log.o \
    $(SRC_DIR)/admin_console.o \
    $(SRC_DIR)/scheduler_helpers.o

# Objets pour le client
OBJ_CLIENT = \
    $(SRC_DIR)/client.o \
    $(SRC_DIR)/utils.o \
    $(SRC_DIR)/log.o

# -------------------------------------------------------------------
# Cibles principales
all: scheduler_server scheduler_client

scheduler_server: $(OBJ_SERVER)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS_SERVER)

scheduler_client: $(OBJ_CLIENT)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS_CLIENT)

# -------------------------------------------------------------------
# Règle générique pour tous les .c qui ont un .h du même nom
# Exemple : *.c et *.h existent tous les deux
$(SRC_DIR)/%.o: $(SRC_DIR)/%.c $(SRC_DIR)/%.h
	$(CC) $(CFLAGS) -c $< -o $@

# -------------------------------------------------------------------
# Règles pour les .c qui n'ont PAS de .h du même nom
# (on les traite explicitement un par un)

# client.c n'a pas de client.h
$(SRC_DIR)/client.o: $(SRC_DIR)/client.c
	$(CC) $(CFLAGS) -c $< -o $@

# scheduler_helpers.c n'a pas de scheduler_helpers.h
$(SRC_DIR)/scheduler_helpers.o: $(SRC_DIR)/scheduler_helpers.c
	$(CC) $(CFLAGS) -c $< -o $@

# Si jamais vous aviez d'autres fichiers .c sans .h, répétez le schéma :
# $(SRC_DIR)/foo.o: $(SRC_DIR)/foo.c
#     $(CC) $(CFLAGS) -c $< -o $@

# -------------------------------------------------------------------
# Pour utils.c et log.c, on utilise la règle générique (ils ont utils.h / log.h)
# Mais si vous préférez les lister « à la main », vous pouvez décommenter :
#
#$(SRC_DIR)/utils.o: $(SRC_DIR)/utils.c $(SRC_DIR)/utils.h
#	$(CC) $(CFLAGS) -c $< -o $@
#
#$(SRC_DIR)/log.o: $(SRC_DIR)/log.c $(SRC_DIR)/log.h
#	$(CC) $(CFLAGS) -Wextra -std=c99 -O2 -pthread -c $< -o $@

# -------------------------------------------------------------------
clean:
	rm -f scheduler_server scheduler_client $(SRC_DIR)/*.o

.PHONY: all clean
