#!/bin/bash
# Script de suivi en direct du build FLX OS (x86_64 et aarch64)
# Florynx Labs — Surveillance en temps réel de la chaîne de compilation

set -u

GREEN="\033[1;32m"
BLUE="\033[1;34m"
CYAN="\033[1;36m"
YELLOW="\033[1;33m"
RED="\033[1;31m"
BOLD="\033[1m"
DIM="\033[2m"
RESET="\033[0m"

clear
echo -e "${CYAN}======================================================${RESET}"
echo -e "${GREEN}${BOLD}         FLX OS — Moniteur de Build en Direct          ${RESET}"
echo -e "${CYAN}======================================================${RESET}"
echo ""

ARCH="$(uname -m)"
echo -e "${BLUE}[i] Architecture hôte détectée :${RESET} ${BOLD}$ARCH${RESET}"

# Déterminer le fichier kernel cible
if [ "$ARCH" = "x86_64" ] || [ "$ARCH" = "amd64" ]; then
    TARGET_KERNEL="build/bzImage"
else
    TARGET_KERNEL="build/Image"
fi

get_container() {
    docker ps --format "{{.ID}} {{.Image}}" | grep -E "flx-toolchain" | awk '{print $1}' | head -n1
}

PREV_STAGE=""

while true; do
    CONTAINER=$(get_container)

    if [ -n "$CONTAINER" ]; then
        IMAGE_NAME=$(docker inspect --format='{{.Config.Image}}' "$CONTAINER" 2>/dev/null || echo "flx-toolchain")
        CREATED=$(docker inspect --format='{{.State.StartedAt}}' "$CONTAINER" 2>/dev/null | cut -d'T' -f2 | cut -d'.' -f1)
        
        echo -e "${YELLOW}${BOLD}[●] Conteneur actif :${RESET} ${CYAN}$CONTAINER${RESET} (${IMAGE_NAME}) depuis ${DIM}$CREATED${RESET}"
        
        # Détection de l'étape courante
        STAGE="Inconnue"
        if docker exec "$CONTAINER" pgrep -f "make.*bzImage|make.*Image" >/dev/null 2>&1; then
            STAGE="1/4 - Compilation du Noyau Linux 6.12"
        elif docker exec "$CONTAINER" pgrep -f "bearssl" >/dev/null 2>&1; then
            STAGE="2/4 - Compilation de BearSSL (Crypto)"
        elif docker exec "$CONTAINER" pgrep -f "make.*obj" >/dev/null 2>&1; then
            STAGE="3/4 - Compilation Userland FLX OS (C Musl)"
        elif docker exec "$CONTAINER" pgrep -f "prepare_assets|gen_init_cpio" >/dev/null 2>&1; then
            STAGE="4/4 - Préparation Assets & Assemblage Initramfs"
        fi

        if [ "$STAGE" != "$PREV_STAGE" ]; then
            echo -e "\n${BOLD}${BLUE}>>> Étape en cours : $STAGE${RESET}"
            PREV_STAGE="$STAGE"
        fi

        # Affichage des 5 dernières lignes du journal kernel ou userland si dispo
        LOG_INFO=$(docker exec "$CONTAINER" sh -c '
            if [ -f /kbuild/kernel-build.log ] && pgrep -f "make.*Image" >/dev/null 2>&1; then
                tail -n 3 /kbuild/kernel-build.log 2>/dev/null
            elif [ -f /kbuild/user-build.log ] && pgrep -f "make" >/dev/null 2>&1; then
                tail -n 3 /kbuild/user-build.log 2>/dev/null
            else
                ps aux | grep -E "gcc|make|ld|strip" | grep -v grep | head -n 2 | awk "{print \$11, \$12, \$13}"
            fi
        ' 2>/dev/null)

        if [ -n "$LOG_INFO" ]; then
            echo -e "${DIM}$LOG_INFO${RESET}"
        fi

        sleep 2
    else
        # Aucun conteneur en cours, vérification des livrables
        if [ -f "$TARGET_KERNEL" ] && [ -f build/initramfs.cpio ] && [ -f build/home.img ]; then
            echo ""
            echo -e "${GREEN}======================================================${RESET}"
            echo -e "${GREEN}${BOLD} [✓] SUCCÈS : FLX OS est entièrement compilé !       ${RESET}"
            echo -e "${GREEN}======================================================${RESET}"
            echo ""
            echo -e "${BOLD}Artefacts générés :${RESET}"
            ls -lh "$TARGET_KERNEL" build/initramfs.cpio build/home.img build/rootfs-files.txt 2>/dev/null
            echo ""
            echo -e "${CYAN}Pour tester l'OS dès maintenant :${RESET}"
            echo -e "  ${BOLD}./run.sh${RESET}"
            echo ""
            if [ -t 0 ]; then
                read -n 1 -s -r -t 5 || true
            else
                sleep 2
            fi
            exit 0
        else
            echo -e "${YELLOW}[*] En attente du lancement d'un conteneur de build...${RESET}"
            sleep 2
        fi
    fi
done
