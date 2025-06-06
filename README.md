# NetScheduler 2.0

**NetScheduler 2.0** est une plateforme client-serveur permettant à plusieurs utilisateurs (connectés simultanément jusqu’à un seuil) d’envoyer en streaming des tâches de compression (Zstd ou FFmpeg pour médias) ou de conversion vidéo→audio (FFmpeg).  
Les tâches sont ordonnancées selon la priorité de l’utilisateur, puis selon la taille du fichier.

## Installation

1. Cloner le dépôt :

   ```bash
   git clone https://github.com/Glom-Companies/NetScheduler.git
   cd NetScheduler

./install_deps.sh
