#pragma once
// Utilidades compartidas: rutas, formato de tiempo, ids de YouTube y ejecución de procesos.
#include <juce_core/juce_core.h>
#include <atomic>
#include <thread>
#include <functional>

namespace yoink
{

/** Literal UTF-8 -> juce::String (sin esto los acentos salen rotos). */
inline juce::String T8 (const char* s) { return juce::String::fromUTF8 (s); }

//==============================================================================
// Rutas
//==============================================================================

/** Raíz de todo lo que guarda el plugin (índice, binarios, temporales). */
inline juce::File rootDir()
{
    auto env = juce::SystemStats::getEnvironmentVariable ("YOINK_DIR", {});
    if (env.isNotEmpty())
        return juce::File (env);
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Application Support").getChildFile ("Yoink");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Yoink");
   #endif
}

inline juce::File indexDir()   { return rootDir().getChildFile ("index"); }
inline juce::File binDir()     { return rootDir().getChildFile ("bin"); }
inline juce::File tmpDir()     { return rootDir().getChildFile ("tmp"); }
inline juce::File sourcesFile(){ return rootDir().getChildFile ("sources.json"); }

/** Índice heredado de la versión DeepSeek (~/.dsh/yoink/index), se lee si existe. */
inline juce::File legacyIndexDir()
{
    auto dsh = juce::SystemStats::getEnvironmentVariable ("DSH_HOME", {});
    auto home = dsh.isNotEmpty() ? juce::File (dsh)
                                 : juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".dsh");
    return home.getChildFile ("yoink").getChildFile ("index");
}

/** Carpeta de clips terminados (~/Yoink). */
inline juce::File clipsDir()
{
    auto env = juce::SystemStats::getEnvironmentVariable ("YOINK_CLIPS", {});
    if (env.isNotEmpty())
        return juce::File (env);
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Yoink");
}

inline void ensureDirs()
{
    for (auto d : { rootDir(), indexDir(), binDir(), tmpDir(), clipsDir() })
        d.createDirectory();
}

//==============================================================================
// Tiempo y nombres
//==============================================================================

/** MM:SS */
inline juce::String stamp (double seconds)
{
    auto total = (int) std::llround (juce::jmax (0.0, seconds));
    return juce::String (total / 60).paddedLeft ('0', 2) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}

/** HH:MM:SS.mmm para --download-sections de yt-dlp. */
inline juce::String clock (double seconds)
{
    auto total = juce::jmax (0.0, seconds);
    auto hours = (int) (total / 3600.0);
    auto minutes = (int) (std::fmod (total, 3600.0) / 60.0);
    auto rest = std::fmod (total, 60.0);
    return juce::String (hours).paddedLeft ('0', 2) + ":" + juce::String (minutes).paddedLeft ('0', 2) + ":"
         + juce::String (rest, 3).paddedLeft ('0', 6);
}

/** Etiqueta libre -> nombre de archivo corto y seguro. */
inline juce::String slug (const juce::String& label)
{
    static const char* fold = "aaaaaaaceeeeiiii" "dnooooo*ouuuuy*s" "aaaaaaaceeeeiiii" "dnooooo*ouuuuy*y";
    juce::String out;
    bool dash = false;
    for (auto p = label.getCharPointer(); ! p.isEmpty(); ++p)
    {
        auto c = (juce::juce_wchar) *p;
        if (c >= 'A' && c <= 'Z')
            c += 32;
        if (c >= 0xC0 && c <= 0xFF && fold[c - 0xC0] != '*')
            c = (juce::juce_wchar) fold[c - 0xC0];

        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        {
            if (dash && out.isNotEmpty())
                out << '-';
            out << (char) c;
            dash = false;
        }
        else
        {
            dash = true;
        }
        if (out.length() >= 40)
            break;
    }
    return out.isEmpty() ? juce::String ("yoink") : out;
}

//==============================================================================
// URLs de YouTube
//==============================================================================

inline bool isVideoId (const juce::String& s)
{
    return s.length() == 11 && s.containsOnly ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-");
}

/** Extrae el id de 11 caracteres de cualquier forma común de URL. */
inline juce::String parseVideoId (const juce::String& input)
{
    auto text = input.trim();
    if (isVideoId (text))
        return text;

    auto take = [] (const juce::String& s) { auto id = s.substring (0, 11); return isVideoId (id) ? id : juce::String(); };

    if (text.contains ("youtu.be/"))
        return take (text.fromFirstOccurrenceOf ("youtu.be/", false, false));

    if (text.contains ("v="))
    {
        auto query = text.fromFirstOccurrenceOf ("?", false, false);
        for (auto part : juce::StringArray::fromTokens (query, "&#", ""))
            if (part.startsWith ("v="))
                if (auto id = take (part.substring (2)); id.isNotEmpty())
                    return id;
    }

    for (auto marker : { "/embed/", "/shorts/", "/live/" })
        if (text.contains (marker))
            if (auto id = take (text.fromFirstOccurrenceOf (marker, false, false)); id.isNotEmpty())
                return id;

    return {};
}

/** Una URL de canal "pelada" devuelve ids de canal: hay que apuntarla a /videos. */
inline juce::String normalizeSourceUrl (const juce::String& url)
{
    auto text = url.trim();
    if (text.isEmpty())
        return text;
    if (text.contains ("list=") || text.contains ("watch?v=") || text.contains ("/shorts/") || text.contains ("youtu.be/"))
        return text;
    while (text.endsWithChar ('/'))
        text = text.dropLastCharacters (1);
    for (auto tab : { "/videos", "/shorts", "/streams", "/playlists" })
        if (text.endsWith (tab))
            return text;
    return text + "/videos";
}

inline juce::String sourceKind (const juce::String& url)
{
    if (url.contains ("list="))
        return "playlist";
    if (url.contains ("youtu.be/") || url.contains ("watch?v=") || url.contains ("/shorts/"))
        return "video";
    return "channel";
}

//==============================================================================
// Procesos externos (yt-dlp, ffmpeg)
//==============================================================================

struct RunResult
{
    int code = -1;
    juce::String output;   // stdout + stderr mezclados
    bool started = false;
    bool timedOut = false;
    bool cancelled = false;

    bool ok() const { return started && ! timedOut && ! cancelled && code == 0; }

    /** Última línea útil (normalmente el ERROR: de yt-dlp). */
    juce::String lastLine() const
    {
        auto lines = juce::StringArray::fromLines (output.trim());
        for (int i = lines.size(); --i >= 0;)
            if (lines[i].startsWith ("ERROR"))
                return lines[i];
        return lines.isEmpty() ? juce::String (yoink::T8 ("código ") + juce::String (code)) : lines[lines.size() - 1];
    }
};

/** Ejecuta un binario, con límite de tiempo y cancelación (lo mata desde un hilo vigía). */
inline RunResult run (const juce::StringArray& args, int timeoutMs, const std::atomic<bool>* cancel = nullptr)
{
    RunResult result;
    juce::ChildProcess process;
    if (! process.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        result.output = "no se pudo ejecutar " + args[0];
        return result;
    }
    result.started = true;

    std::atomic<bool> done { false }, timedOut { false }, cancelled { false };
    std::thread watchdog ([&]
    {
        auto start = juce::Time::getMillisecondCounter();
        while (! done.load())
        {
            if (cancel != nullptr && cancel->load())
            {
                cancelled = true;
                process.kill();
                return;
            }
            if ((int) (juce::Time::getMillisecondCounter() - start) > timeoutMs)
            {
                timedOut = true;
                process.kill();
                return;
            }
            juce::Thread::sleep (50);
        }
    });

    result.output = process.readAllProcessOutput();
    process.waitForProcessToFinish (5000);
    done = true;
    watchdog.join();

    result.code = (int) process.getExitCode();
    result.timedOut = timedOut.load();
    result.cancelled = cancelled.load();
    return result;
}

//==============================================================================
// HTTP
//==============================================================================

struct HttpResponse
{
    int status = 0;
    juce::String body;
    bool ok() const { return status >= 200 && status < 300; }
};

inline HttpResponse httpRequest (const juce::String& url, const juce::String& headers,
                                 const juce::String& postBody = {}, int timeoutMs = 20000)
{
    HttpResponse response;
    juce::URL target (url);
    if (postBody.isNotEmpty())
        target = target.withPOSTData (postBody);

    const bool post = postBody.isNotEmpty();
    const auto options = juce::URL::InputStreamOptions (post ? juce::URL::ParameterHandling::inPostData
                                                             : juce::URL::ParameterHandling::inAddress)
                             .withExtraHeaders (headers)
                             .withConnectionTimeoutMs (timeoutMs)
                             .withNumRedirectsToFollow (8)
                             .withStatusCode (&response.status)
                             .withHttpRequestCmd (post ? "POST" : "GET");

    if (auto stream = target.createInputStream (options))
        response.body = stream->readEntireStreamAsString();
    return response;
}

} // namespace yoink
