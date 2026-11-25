# Yoink

Buscá audio por **lo que dice la gente** en videos de YouTube, recortá la frase a un WAV y
tocala por MIDI o arrastrala directo al DAW.

Plugin instrumento (AU / VST3) y app standalone, hecho en JUCE. Port del bundle original de
DeepSeek Harness (`yoink`), con la misma lógica: índice local de subtítulos, búsqueda BM25 +
frase exacta, recorte con yt-dlp + ffmpeg.

## Instalar (macOS)

1. Bajá `Yoink-macOS.zip` desde **Actions → último build → Artifacts** (o desde Releases si
   hay un tag `v*`) y descomprimilo.
2. Doble clic en `Instalar.command` (si macOS se queja: clic derecho → Abrir).
3. En Logic: pista de **Instrumento de software → Instrumentos AU → 0600musica → Yoink**.

## Uso

1. **Frase** — escribí lo que dice alguien y Enter.
   - Con **Todo YouTube** prendido (por defecto): Yoink le pasa la frase al buscador de
     YouTube, toma los primeros 20/40/80 videos, les baja los subtítulos en paralelo y te
     muestra el segundo exacto donde se dice. Los resultados aparecen a medida que revisa.
     Todo lo que baja queda en tu índice.
   - Con **Todo YouTube** apagado: busca solo en tu índice local (instantáneo, sin red).
   - No importan tildes, mayúsculas ni puntuación. Las frases exactas salen primero.
2. **Fuente** (opcional) — pegá la URL de un video, playlist o canal y tocá **Indexar**
   para sumar material propio al índice.
3. **Yoink!** (o doble clic en un resultado) — baja solo ese tramo y lo corta a WAV
   48 kHz / 24 bit / estéreo en `~/Yoink`.
   - *Duración*: largo del clip. *Desfase*: los subtítulos marcan el segundo redondeado,
     así que arrancar medio segundo antes suele caer mejor.
4. **El clip** — arrastrá la onda a una pista del DAW, o tocalo por MIDI (Do3 = altura
   original, el resto transpone). *Escuchar* lo dispara sin teclado.

La primera vez que indexás o recortás se bajan `yt-dlp` y `ffmpeg` (~100 MB) a
`~/Library/Application Support/Yoink/bin`. Si ya los tenés por Homebrew, usa esos.
Si YouTube cambia algo y deja de bajar, tocá **Actualizar yt-dlp**.

## Dónde queda cada cosa

| Ruta | Qué es |
| --- | --- |
| `~/Library/Application Support/Yoink/index/` | Un JSON por video (mismo formato que la versión DeepSeek) |
| `~/.dsh/yoink/index/` | Si existe, también se lee (índice de la versión anterior) |
| `~/Library/Application Support/Yoink/bin/` | yt-dlp y ffmpeg descargados |
| `~/Yoink/` | Clips terminados |

Variables para pruebas: `YOINK_DIR`, `YOINK_CLIPS`, `YOINK_LANGS` (por defecto `es,en`),
`YOINK_YTDLP`, `YOINK_FFMPEG`.

## Compilar

```bash
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git JUCE
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/YoinkTests_artefacts/Release/YoinkTests   # tests del motor, sin red
```

GitHub Actions compila AU + VST3 + Standalone universales (Apple Silicon + Intel), corre los
tests y `auval`, firma ad-hoc y sube el zip.

## Límites

- Solo sirven videos con subtítulos (manuales o automáticos); no hay reconocimiento de voz local.
- "Todo YouTube" depende del buscador de YouTube: si el video que dice tu frase no sale
  entre los primeros candidatos, no aparece. Más candidatos = más lento pero más chances.
  No existe una forma pública de buscar en el audio de *todos* los videos.
- Los subtítulos automáticos se equivocan: si no encontrás algo, probá menos palabras.
- Bajar audio de YouTube puede ir contra sus términos de uso; usalo con material que tengas
  derecho a samplear.
