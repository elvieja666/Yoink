#!/bin/bash
# Copia Yoink a las carpetas de plugins del usuario y le quita la cuarentena de macOS.
cd "$(dirname "$0")"
set -e
mkdir -p ~/Library/Audio/Plug-Ins/Components ~/Library/Audio/Plug-Ins/VST3
rm -rf ~/Library/Audio/Plug-Ins/Components/Yoink.component ~/Library/Audio/Plug-Ins/VST3/Yoink.vst3
cp -R Yoink.component ~/Library/Audio/Plug-Ins/Components/
cp -R Yoink.vst3 ~/Library/Audio/Plug-Ins/VST3/
if [ -d Yoink.app ]; then rm -rf /Applications/Yoink.app; cp -R Yoink.app /Applications/ || true; fi
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Yoink.component ~/Library/Audio/Plug-Ins/VST3/Yoink.vst3 2>/dev/null || true
xattr -dr com.apple.quarantine /Applications/Yoink.app 2>/dev/null || true
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo
echo "Listo. Yoink quedó instalado (AU, VST3 y la app en /Aplicaciones)."
echo "En Logic: pista de Instrumento de software → Instrumentos AU → 0600musica → Yoink"
echo "(si no aparece: Logic Pro → Ajustes → Plug-in Manager → Reset & Rescan Selection)"
