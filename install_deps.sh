#!/bin/sh
set -e

echo "Mise à jour des paquets…"
sudo apt-get update

echo "Installation de ncurses (runtime + dev)…"
sudo apt-get install -y libncurses5 libncurses5-dev libncursesw5-dev

echo "Installation de FFmpeg…"
sudo apt-get install -y ffmpeg

echo "Installation de Zstd (libzstd)…"
sudo apt-get install -y zstd libzstd-dev

echo "Dépendances installées."

chmod +x install_deps.sh
