#!/usr/bin/env python3
"""Replace assets/icones images with icons from the 4K cropped FLX OS pack."""

import argparse
import shutil
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
PACK = ROOT / "assets" / "FLX_OS_Icon_Pack_4K_Cropped"
ICONS = ROOT / "assets" / "icones"

ICON_MAP = {
    "01_explorateur_de_fichiers.png": ("file-explorer.png",),
    "02_navigateur.png": ("flw-browser.png", "brower_icone.png"),
    "03_terminal.png": ("terminal.png",),
    "04_parametres.png": ("settings.png",),
    "06_calculatrice.png": ("calculator_icones.png",),
    "07_calendrier.png": ("calandar.png",),
    "08_horloge.png": ("clock.png",),
    "09_photos.png": ("galerie_images_icone.png",),
    "10_musique.png": ("media_palyer_icone.png",),
    "11_videos.png": ("videos.png",),
    "12_documents.png": ("documents.png",),
    "13_telechargements.png": ("downloads.png",),
    "15_corbeille.png": ("trash.png",),
    "16_bloc_notes.png": ("note_app_icone.png",),
    "17_paint.png": ("paint.png",),
    "18_securite.png": ("security.png",),
    "19_reseau.png": ("network.png",),
    "20_bluetooth.png": ("bluetooth.png",),
    "21_wifi.png": ("wifi.png",),
    "22_son.png": ("sound.png",),
    "23_batterie.png": ("battery.png",),
    "24_alimentation.png": ("power.png",),
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="list the files that would be copied without changing anything",
    )
    args = parser.parse_args()

    missing = [name for name in ICON_MAP if not (PACK / name).is_file()]
    if missing:
        print(f"Pack incomplet dans {PACK} :", file=sys.stderr)
        for name in missing:
            print(f"  - {name}", file=sys.stderr)
        return 1
    if not ICONS.is_dir():
        print(f"Dossier d’icônes introuvable : {ICONS}", file=sys.stderr)
        return 1

    for source_name, target_names in ICON_MAP.items():
        source = PACK / source_name
        for target_name in target_names:
            target = ICONS / target_name
            if args.dry_run:
                action = "serait copié vers"
            else:
                shutil.copy2(source, target)
                action = "copié vers"
            print(f"{source.name} {action} {target.relative_to(ROOT)}")

    if args.dry_run:
        print("Prévisualisation uniquement : aucun fichier n’a été modifié.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
