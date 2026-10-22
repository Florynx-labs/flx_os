#!/usr/bin/env python3
"""Mass rebrand: Windows 12 -> FLX OS in all C source files."""
import os

replacements = [
    ("Windows 12", "FLX OS"),
    ("windows12", "flx-os"),
    ("w12-netd", "flx-netd"),
    ("w12-compositor", "flx-compositor"),
    ("w12sh", "flxsh"),
    ("user@windows12", "flx@flx-os"),
    ("windows12\", 9", 'flx-os", 6'),
    ("Windows 12 proof", "FLX OS - Informations systeme"),
    ("not affiliated with Microsoft", "par Florynx Labs"),
    ("unofficial fan project", "logiciel libre"),
    ("Personalize Windows 12", "Configuration de FLX OS"),
    ("written for Windows 12", "ecrit pour FLX OS"),
    ("What runs on this PC", "Informations systeme FLX OS"),
    ("Records the Windows 12 video", "Enregistrer FLX OS"),
    ("W12Browser/1.0", "FLXBrowser/1.0"),
    ("Mozilla/5.0 (Linux; Windows 12; Mobile)", "Mozilla/5.0 (Linux; FLX OS 0.1; Mobile)"),
    ("Mozilla/5.0 (Windows 12; ARM64)", "Mozilla/5.0 (FLX OS 0.1; ARM64)"),
    ("Windows 12 processes", "Processus FLX OS"),
    ("App \u00b7 press Enter to open", "Application \u00b7 Entr\u00e9e pour ouvrir"),
    ("Best match", "Meilleur r\u00e9sultat"),
    ("No results", "Aucun r\u00e9sultat"),
]

src_root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src')

updated = []
for root, dirs, files in os.walk(src_root):
    for fname in files:
        if not (fname.endswith('.c') or fname.endswith('.h')):
            continue
        path = os.path.join(root, fname)
        with open(path, 'rb') as f:
            content = f.read().decode('utf-8', errors='replace')
        orig = content
        for old, new in replacements:
            content = content.replace(old, new)
        if content != orig:
            with open(path, 'wb') as f:
                f.write(content.encode('utf-8'))
            updated.append(path)
            print(f"  Updated: {path}")

print(f"\nTotal files updated: {len(updated)}")
