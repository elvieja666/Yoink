#pragma once
// yt-dlp y ffmpeg: se usan los descargados en Application Support/Yoink/bin, o los de Homebrew,
// y si no hay ninguno se bajan la primera vez (~100 MB en total).
#include "Util.h"

namespace yoink
{

inline bool usableBinary (const juce::File& f)
{
    return f.existsAsFile() && f.getSize() > 1000000;
}

using ProgressFn = std::function<void (const juce::String&)>;

/** Descarga en streaming a un temporal y lo mueve al destino solo si llegó completo. */
inline juce::Result downloadBinary (const juce::String& url, const juce::File& dest, const ProgressFn& progress,
                                    const std::atomic<bool>* cancel)
{
    int status = 0;
    auto stream = juce::URL (url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                         .withConnectionTimeoutMs (30000)
                                                         .withNumRedirectsToFollow (10)
                                                         .withStatusCode (&status));
    if (stream == nullptr || (status != 0 && status >= 400))
        return juce::Result::fail ("no se pudo descargar " + url + " (HTTP " + juce::String (status) + ")");

    dest.getParentDirectory().createDirectory();
    auto partial = dest.getSiblingFile (dest.getFileName() + ".part");
    partial.deleteFile();
    {
        juce::FileOutputStream out (partial);
        if (! out.openedOk())
            return juce::Result::fail ("no se pudo escribir " + partial.getFullPathName());

        auto total = stream->getTotalLength();
        juce::HeapBlock<char> buffer (1 << 16);
        juce::int64 received = 0, lastReport = 0;
        for (;;)
        {
            if (cancel != nullptr && cancel->load())
                return juce::Result::fail ("cancelado");
            auto n = stream->read (buffer.get(), 1 << 16);
            if (n <= 0)
                break;
            out.write (buffer.get(), (size_t) n);
            received += n;
            if (progress && received - lastReport > (2 << 20))
            {
                lastReport = received;
                progress ("Descargando " + dest.getFileName() + yoink::T8 ("… ")
                          + (total > 0 ? juce::String ((int) (100 * received / total)) + "%"
                                       : juce::String (received >> 20) + " MB"));
            }
        }
        out.flush();
    }

    if (! usableBinary (partial))
    {
        partial.deleteFile();
        return juce::Result::fail ("la descarga de " + dest.getFileName() + yoink::T8 (" quedó incompleta"));
    }
    dest.deleteFile();
    partial.moveFileTo (dest);
   #if JUCE_MAC || JUCE_LINUX
    dest.setExecutePermission (true);
   #endif
   #if JUCE_MAC
    // por las dudas: sin cuarentena, si no Gatekeeper lo bloquea
    run ({ "/usr/bin/xattr", "-c", dest.getFullPathName() }, 10000);
   #endif
    return juce::Result::ok();
}

inline juce::File firstExisting (std::initializer_list<const char*> paths)
{
    for (auto p : paths)
        if (juce::File f (p); f.existsAsFile())
            return f;
    return {};
}

/** yt-dlp utilizable; descarga el build standalone si no hay. */
inline juce::File ensureYtDlp (const ProgressFn& progress, const std::atomic<bool>* cancel, juce::String& error)
{
    auto bundled = binDir().getChildFile ("yt-dlp");
    if (usableBinary (bundled))
        return bundled;

    auto env = juce::SystemStats::getEnvironmentVariable ("YOINK_YTDLP", {});
    if (env.isNotEmpty() && juce::File (env).existsAsFile())
        return juce::File (env);

    if (auto brew = firstExisting ({ "/opt/homebrew/bin/yt-dlp", "/usr/local/bin/yt-dlp" }); brew != juce::File())
        return brew;

   #if JUCE_MAC
    const juce::String url = "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_macos";
   #elif JUCE_WINDOWS
    const juce::String url = "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe";
    bundled = bundled.withFileExtension ("exe");
   #else
    const juce::String url = "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_linux";
   #endif
    if (progress) progress (yoink::T8 ("Descargando yt-dlp (solo la primera vez)…"));
    auto r = downloadBinary (url, bundled, progress, cancel);
    if (r.failed()) { error = r.getErrorMessage(); return {}; }
    return bundled;
}

/** ffmpeg utilizable; descarga un build estático si no hay. */
inline juce::File ensureFfmpeg (const ProgressFn& progress, const std::atomic<bool>* cancel, juce::String& error)
{
    auto bundled = binDir().getChildFile ("ffmpeg");
    if (usableBinary (bundled))
        return bundled;

    auto env = juce::SystemStats::getEnvironmentVariable ("YOINK_FFMPEG", {});
    if (env.isNotEmpty() && juce::File (env).existsAsFile())
        return juce::File (env);

    if (auto brew = firstExisting ({ "/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg" }); brew != juce::File())
        return brew;

   #if JUCE_MAC
    #if defined (__aarch64__) || defined (__arm64__)
    const juce::String url = "https://github.com/eugeneware/ffmpeg-static/releases/latest/download/ffmpeg-darwin-arm64";
    #else
    const juce::String url = "https://github.com/eugeneware/ffmpeg-static/releases/latest/download/ffmpeg-darwin-x64";
    #endif
   #elif JUCE_WINDOWS
    const juce::String url = "https://github.com/eugeneware/ffmpeg-static/releases/latest/download/ffmpeg-win32-x64";
    bundled = bundled.withFileExtension ("exe");
   #else
    const juce::String url = "https://github.com/eugeneware/ffmpeg-static/releases/latest/download/ffmpeg-linux-x64";
   #endif
    if (progress) progress (yoink::T8 ("Descargando ffmpeg (solo la primera vez)…"));
    auto r = downloadBinary (url, bundled, progress, cancel);
    if (r.failed()) { error = r.getErrorMessage(); return {}; }
    return bundled;
}

} // namespace yoink
