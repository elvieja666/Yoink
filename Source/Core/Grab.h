#pragma once
// Recorte: baja solo el tramo con yt-dlp --download-sections y lo convierte con ffmpeg
// a WAV 48 kHz / 24 bit / estéreo. Si la descarga por tramo falla, baja todo y corta con -ss.
#include "Bins.h"

namespace yoink
{

struct GrabRequest
{
    juce::String videoId, label;
    double start = 0, duration = 6;
};

struct GrabResult
{
    juce::File file;
    juce::String error;
    bool ok() const { return error.isEmpty() && file.existsAsFile(); }
};

inline juce::File firstMedia (const juce::File& dir, const juce::String& prefix)
{
    for (auto& f : dir.findChildFiles (juce::File::findFiles, false))
    {
        auto name = f.getFileName();
        if (! name.startsWith (prefix) || name.endsWith (".part") || name.endsWith (".ytdl") || name.endsWith (".json3"))
            continue;
        if (f.getSize() > 0)
            return f;
    }
    return {};
}

inline GrabResult grabClip (const GrabRequest& request, const ProgressFn& progress, const std::atomic<bool>* cancel)
{
    ensureDirs();
    auto videoId = request.videoId.trim();
    if (! isVideoId (videoId))
        return { {}, yoink::T8 ("id de video inválido: ") + videoId };

    const double begin = juce::jmax (0.0, request.start);
    const double length = juce::jlimit (0.5, 120.0, request.duration);

    juce::String error;
    auto ytDlp = ensureYtDlp (progress, cancel, error);
    if (ytDlp == juce::File()) return { {}, error };
    auto ffmpeg = ensureFfmpeg (progress, cancel, error);
    if (ffmpeg == juce::File()) return { {}, error };

    auto work = tmpDir().getChildFile ("grab-" + videoId + "-" + juce::String (juce::Time::currentTimeMillis()));
    work.createDirectory();
    juce::ScopeGuard cleanup { [work] { work.deleteRecursively(); } };

    const auto url = "https://www.youtube.com/watch?v=" + videoId;
    if (progress) progress ("Bajando " + stamp (begin) + " de " + videoId + yoink::T8 ("…"));

    auto section = run ({ ytDlp.getFullPathName(), "-f", "bestaudio/best", "--no-playlist", "--no-warnings",
                          "--ffmpeg-location", ffmpeg.getFullPathName(),
                          "--download-sections", "*" + clock (begin) + "-" + clock (begin + length),
                          "-o", work.getChildFile ("section.%(ext)s").getFullPathName(), url },
                        300000, cancel);
    if (section.cancelled) return { {}, "cancelado" };

    auto media = firstMedia (work, "section");
    const bool fromSection = media != juce::File();
    if (! fromSection)
    {
        if (progress) progress (yoink::T8 ("El tramo no bajó; bajando el audio completo…"));
        auto full = run ({ ytDlp.getFullPathName(), "-f", "bestaudio/best", "--no-playlist", "--no-warnings",
                           "-o", work.getChildFile ("full.%(ext)s").getFullPathName(), url },
                         600000, cancel);
        if (full.cancelled) return { {}, "cancelado" };
        media = firstMedia (work, "full");
        if (media == juce::File())
            return { {}, "yt-dlp no pudo bajar " + videoId + ": " + (full.output.isNotEmpty() ? full.lastLine() : section.lastLine()) };
    }

    clipsDir().createDirectory();
    auto name = slug (request.label) + "-" + videoId + "-" + stamp (begin).removeCharacters (":");
    auto output = clipsDir().getChildFile (name + ".wav");
    for (int counter = 2; output.exists(); ++counter)
        output = clipsDir().getChildFile (name + "-" + juce::String (counter) + ".wav");

    if (progress) progress (yoink::T8 ("Cortando el WAV…"));
    juce::StringArray args { ffmpeg.getFullPathName(), "-hide_banner", "-loglevel", "error", "-y" };
    if (! fromSection)
        args.addArray ({ "-ss", juce::String (begin, 3) });
    args.addArray ({ "-i", media.getFullPathName(), "-t", juce::String (length, 3),
                     "-map_metadata", "-1", "-ar", "48000", "-ac", "2", "-c:a", "pcm_s24le", output.getFullPathName() });

    auto cut = run (args, 180000, cancel);
    if (! cut.ok() || ! output.existsAsFile())
    {
        output.deleteFile();
        return { {}, "ffmpeg no pudo cortar el clip: " + cut.lastLine() };
    }
    return { output, {} };
}

/** yt-dlp -U (solo sirve con el binario descargado, no con el de Homebrew). */
inline juce::String updateYtDlp (const std::atomic<bool>* cancel)
{
    juce::String error;
    auto ytDlp = ensureYtDlp ({}, cancel, error);
    if (ytDlp == juce::File())
        return error;
    if (ytDlp.getFullPathName().contains ("homebrew") || ytDlp.getFullPathName().startsWith ("/usr/local"))
        return "yt-dlp es el de Homebrew: actualizalo con  brew upgrade yt-dlp";
    auto res = run ({ ytDlp.getFullPathName(), "-U" }, 180000, cancel);
    return res.lastLine();
}

} // namespace yoink
