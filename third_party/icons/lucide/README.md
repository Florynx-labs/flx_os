# Lucide icon sources

This directory vendors the 68 monochrome SVGs used by FLX OS from Lucide
0.468.0 (`lucide-icons/lucide`, ISC; license text is in `LICENSE`). The selected
icons are rendered at build time by `scripts/prepare_assets.py` into white RGBA
masks at 16, 20, 24, 32, 48, and 64 pixels. libflxui applies the active theme
color when drawing them. No SVG renderer or icon package is included in the OS.
