#!/bin/bash
# Script de suivi en direct du build FLX OS

echo -e "\033[1;36m======================================================\033[0m"
echo -e "\033[1;32m         FLX OS - Compilation en Direct              \033[0m"
echo -e "\033[1;36m======================================================\033[0m"
echo ""

while true; do
    CONTAINER=$(docker ps -q --filter ancestor=flx-toolchain | head -n1)
    if [ -n "$CONTAINER" ]; then
        echo -e "\033[1;33m[+] Conteneur de build actif : $CONTAINER\033[0m"
        
        # Follow kernel build log if running
        docker exec -t "$CONTAINER" sh -c '
            if [ -f /kbuild/kernel-build.log ]; then
                echo -e "\n--- [1/3] Compilation du Noyau Linux ARM64 ---"
                tail -n 20 -f /kbuild/kernel-build.log &
                KPID=$!
                while pgrep -f "make.*Image" >/dev/null 2>&1; do sleep 1; done
                kill $KPID 2>/dev/null || true
            fi
            if [ -f /kbuild/user-build.log ]; then
                echo -e "\n--- [2/3] Compilation du Userland & Outils ---"
                tail -n 20 -f /kbuild/user-build.log &
                UPID=$!
                while pgrep -f "make.*userland" >/dev/null 2>&1; do sleep 1; done
                kill $UPID 2>/dev/null || true
            fi
        '
    else
        if [ -f build/Image ] && [ -f build/initramfs.cpio ] && [ -f build/home.img ]; then
            echo ""
            echo -e "\033[1;32m======================================================\033[0m"
            echo -e "\033[1;32m [✓] SUCCÈS : Le build FLX OS est terminé !           \033[0m"
            echo -e "\033[1;32m======================================================\033[0m"
            ls -lh build/
            echo ""
            echo "L'OS est prêt à être lancé."
            echo "Appuyez sur n'importe quelle touche pour fermer ce terminal..."
            read -n 1 -s
            exit 0
        else
            echo -e "\033[1;33m[*] En attente de l'étape suivante...\033[0m"
            sleep 2
        fi
    fi
    sleep 2
done
