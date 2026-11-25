#pragma once
// Subtítulos: InnerTube con el cliente IOS (sus URLs de subtítulos no piden token),
// y si falla, yt-dlp --write-auto-subs. Formato json3 en ambos casos.
#include "Store.h"
#include "Bins.h"

namespace yoink
{

inline juce::StringArray captionLangs()
{
    auto env = juce::SystemStats::getEnvironmentVariable ("YOINK_LANGS", "es,en");
    auto langs = juce::StringArray::fromTokens (env, ",", "");
    langs.trim();
    langs.removeEmptyStrings();
    return langs;
}

static constexpr const char* kWebUA = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36";
static constexpr const char* kIosUA = "com.google.ios.youtube/20.10.4 (iPhone16,2; U; CPU iOS 18_3_2 like Mac OS X;)";
static constexpr const char* kIosVersion = "20.10.4";

/** Payload json3 -> líneas con tiempo; descarta repeticiones consecutivas de los auto-subs. */
inline std::vector<Line> linesFromJson3 (const juce::var& payload)
{
    std::vector<Line> lines;
    auto* events = payload["events"].getArray();
    if (events == nullptr)
        return lines;
    juce::String previous;
    for (auto& ev : *events)
    {
        auto* segs = ev["segs"].getArray();
        if (segs == nullptr)
            continue;
        juce::String text;
        for (auto& seg : *segs)
            text << seg["utf8"].toString();
        text = text.replaceCharacters ("\n\r\t", "   ");
        while (text.contains ("  "))
            text = text.replace ("  ", " ");
        text = text.trim();
        if (text.isEmpty() || text == previous)
            continue;
        previous = text;
        auto ms = ev["tStartMs"].isVoid() ? 0.0 : (double) ev["tStartMs"];
        lines.push_back ({ (int) std::llround (juce::jmax (0.0, ms) / 1000.0), text });
    }
    return lines;
}

/** Elige pista según el orden de idiomas; las manuales ganan en empate. */
inline juce::var pickTrack (const juce::var& tracks, const juce::StringArray& langs)
{
    auto* arr = tracks.getArray();
    if (arr == nullptr || arr->isEmpty())
        return {};
    juce::var best;
    int bestScore = 1 << 30;
    for (auto& t : *arr)
    {
        auto lang = t["languageCode"].toString();
        bool manual = t["kind"].toString() != "asr";
        int index = -1;
        for (int i = 0; i < langs.size(); ++i)
            if (lang == langs[i] || lang.startsWith (langs[i] + "-"))
            {
                index = i;
                break;
            }
        int score = (index >= 0 ? index * 2 : 100) + (manual ? 0 : 1);
        if (score < bestScore)
        {
            bestScore = score;
            best = t;
        }
    }
    return best;
}

/** Busca el objeto JSON que empieza después de `marker`, contando llaves. */
inline juce::var extractObject (const juce::String& html, const juce::String& marker)
{
    auto at = html.indexOf (marker);
    if (at < 0)
        return {};
    auto start = html.indexOfChar (at, '{');
    if (start < 0)
        return {};
    int depth = 0;
    bool inString = false, escaped = false;
    auto p = html.getCharPointer() + start;
    for (int i = start; ! p.isEmpty(); ++p, ++i)
    {
        auto c = *p;
        if (inString)
        {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') inString = true;
        else if (c == '{') ++depth;
        else if (c == '}' && --depth == 0)
            return juce::JSON::parse (html.substring (start, i + 1));
    }
    return {};
}

inline juce::String matchAfter (const juce::String& html, const juce::String& key)
{
    auto marker = "\"" + key + "\":\"";
    auto at = html.indexOf (marker);
    if (at < 0)
        return {};
    return html.substring (at + marker.length()).upToFirstOccurrenceOf ("\"", false, false);
}

struct Transcript
{
    Record record;
    juce::String error;
    bool ok() const { return error.isEmpty() && ! record.lines.empty(); }
};

inline Transcript viaInnerTube (const juce::String& videoId)
{
    Transcript out;
    const juce::String webHeaders = juce::String ("User-Agent: ") + kWebUA
                                  + "\r\nAccept-Language: es-ES,es;q=0.9,en;q=0.7\r\nCookie: SOCS=CAI";
    auto page = httpRequest ("https://www.youtube.com/watch?v=" + videoId + "&hl=es&gl=ES", webHeaders);
    if (! page.ok() && page.body.isEmpty())
        return { {}, yoink::T8 ("página del video HTTP ") + juce::String (page.status) };

    auto key = matchAfter (page.body, "INNERTUBE_API_KEY");
    auto visitor = matchAfter (page.body, "VISITOR_DATA");
    if (key.isEmpty())
        return { {}, yoink::T8 ("la página no trae clave InnerTube") };

    auto* client = new juce::DynamicObject();
    client->setProperty ("clientName", "IOS");
    client->setProperty ("clientVersion", kIosVersion);
    client->setProperty ("deviceModel", "iPhone16,2");
    client->setProperty ("osName", "iPhone");
    client->setProperty ("osVersion", "18.3.2.22D82");
    client->setProperty ("hl", "es");
    client->setProperty ("gl", "ES");
    auto* context = new juce::DynamicObject();
    context->setProperty ("client", juce::var (client));
    auto* body = new juce::DynamicObject();
    body->setProperty ("videoId", videoId);
    body->setProperty ("context", juce::var (context));

    juce::String headers = "Content-Type: application/json\r\nUser-Agent: " + juce::String (kIosUA)
                         + "\r\nX-Youtube-Client-Name: 5\r\nX-Youtube-Client-Version: " + kIosVersion;
    if (visitor.isNotEmpty())
        headers << "\r\nX-Goog-Visitor-Id: " << visitor;

    auto resp = httpRequest ("https://www.youtube.com/youtubei/v1/player?key=" + key + "&prettyPrint=false",
                             headers, juce::JSON::toString (juce::var (body), true));
    if (! resp.ok())
        return { {}, "player HTTP " + juce::String (resp.status) };

    auto player = juce::JSON::parse (resp.body);
    auto status = player["playabilityStatus"]["status"].toString();
    if (status.isNotEmpty() && status != "OK")
        return { {}, ("estado " + status + ": " + player["playabilityStatus"]["reason"].toString()).trim() };

    auto track = pickTrack (player["captions"]["playerCaptionsTracklistRenderer"]["captionTracks"], captionLangs());
    auto baseUrl = track["baseUrl"].toString();
    if (baseUrl.isEmpty())
        return { {}, yoink::T8 ("el video no tiene subtítulos") };

    auto trackUrl = baseUrl.contains ("fmt=") ? baseUrl : baseUrl + "&fmt=json3";
    auto subs = httpRequest (trackUrl, webHeaders);
    if (! subs.ok())
        return { {}, yoink::T8 ("subtítulos HTTP ") + juce::String (subs.status) };
    if (! subs.body.trimStart().startsWithChar ('{'))
        return { {}, yoink::T8 ("los subtítulos no vinieron en json3 (") + juce::String (subs.body.length()) + " bytes)" };

    auto& r = out.record;
    r.lines = linesFromJson3 (juce::JSON::parse (subs.body));
    if (r.lines.empty())
        return { {}, yoink::T8 ("pista de subtítulos vacía") };

    auto details = player["videoDetails"];
    r.videoId = videoId;
    r.title = details["title"].toString();
    r.channel = details["author"].toString();
    r.duration = details["lengthSeconds"].toString().getDoubleValue();
    r.lang = track["languageCode"].toString();
    r.asr = track["kind"].toString() == "asr";
    r.source = "innertube";
    return out;
}

inline void oEmbed (Record& r)
{
    auto resp = httpRequest ("https://www.youtube.com/oembed?url=https://www.youtube.com/watch?v=" + r.videoId + "&format=json",
                             juce::String ("User-Agent: ") + kWebUA, {}, 10000);
    if (! resp.ok())
        return;
    auto v = juce::JSON::parse (resp.body);
    r.title = v["title"].toString();
    r.channel = v["author_name"].toString();
}

inline Transcript viaYtDlp (const juce::String& videoId, const juce::File& ytDlp, const std::atomic<bool>* cancel)
{
    auto work = tmpDir().getChildFile ("subs-" + videoId + "-" + juce::String (juce::Time::currentTimeMillis()));
    work.createDirectory();
    auto cleanup = juce::ScopeGuard { [work] { work.deleteRecursively(); } };

    auto res = run ({ ytDlp.getFullPathName(), "--skip-download", "--write-subs", "--write-auto-subs",
                      "--sub-langs", captionLangs().joinIntoString (","), "--sub-format", "json3", "--no-warnings",
                      "-o", work.getChildFile ("%(id)s.%(ext)s").getFullPathName(),
                      "https://www.youtube.com/watch?v=" + videoId },
                    120000, cancel);

    auto files = work.findChildFiles (juce::File::findFiles, false, "*.json3");
    if (files.isEmpty())
        return { {}, yoink::T8 ("yt-dlp no bajó subtítulos (") + res.lastLine() + ")" };

    // preferir el idioma según el orden configurado
    auto langs = captionLangs();
    auto chosen = files[0];
    int bestIndex = 1 << 30;
    for (auto& f : files)
        for (int i = 0; i < langs.size(); ++i)
            if (f.getFileName().contains ("." + langs[i]) && i < bestIndex)
            {
                bestIndex = i;
                chosen = f;
            }

    Transcript out;
    auto& r = out.record;
    r.lines = linesFromJson3 (juce::JSON::parse (chosen.loadFileAsString()));
    if (r.lines.empty())
        return { {}, yoink::T8 ("el archivo de subtítulos de yt-dlp está vacío") };
    r.videoId = videoId;
    auto parts = juce::StringArray::fromTokens (chosen.getFileName(), ".", "");
    r.lang = parts.size() >= 3 ? parts[parts.size() - 2] : juce::String();
    r.asr = false;
    r.source = "yt-dlp";
    oEmbed (r);
    return out;
}

inline Transcript fetchTranscript (const juce::String& videoId, const juce::File& ytDlp, const std::atomic<bool>* cancel)
{
    auto first = viaInnerTube (videoId);
    if (first.ok())
        return first;
    if (cancel != nullptr && cancel->load())
        return { {}, "cancelado" };
    auto second = viaYtDlp (videoId, ytDlp, cancel);
    if (second.ok())
        return second;
    return { {}, yoink::T8 ("sin subtítulos: ") + first.error + "; alternativa: " + second.error };
}

//==============================================================================
// Expandir fuentes (playlist / canal) con yt-dlp

struct Entry
{
    juce::String videoId, title, channel;
};

inline std::vector<Entry> listEntries (const juce::File& ytDlp, const juce::String& url, int limit,
                                       const std::atomic<bool>* cancel, juce::String& error)
{
    const bool isSearch = url.startsWith ("ytsearch");
    auto direct = isSearch ? juce::String() : parseVideoId (url);
    if (direct.isNotEmpty() && ! url.contains ("list="))
        return { { direct, {}, {} } };

    auto target = isSearch ? url : normalizeSourceUrl (url);
    auto res = run ({ ytDlp.getFullPathName(), "-J", "--flat-playlist", "--playlist-end", juce::String (limit),
                      "--no-warnings", target },
                    240000, cancel);

    // stdout y stderr vienen mezclados: el JSON es la línea que empieza con '{'
    juce::var payload;
    for (auto& line : juce::StringArray::fromLines (res.output))
        if (line.startsWithChar ('{'))
        {
            payload = juce::JSON::parse (line);
            if (payload.isObject())
                break;
        }

    if (! payload.isObject())
    {
        error = (isSearch ? yoink::T8 ("la búsqueda en YouTube falló: ") : "yt-dlp no pudo expandir " + url + ": ")
              + res.lastLine();
        return {};
    }

    std::vector<Entry> out;
    auto add = [&] (const juce::var& row)
    {
        auto id = row["id"].toString();
        if (isVideoId (id) && (int) out.size() < limit)
            out.push_back ({ id, row["title"].toString(),
                             row["channel"].toString().isNotEmpty() ? row["channel"].toString() : row["uploader"].toString() });
    };
    if (auto* entries = payload["entries"].getArray())
        for (auto& row : *entries)
            add (row);
    else
        add (payload);
    return out;
}

/** Candidatos del buscador de YouTube para una frase (entre comillas: busca la frase exacta). */
inline std::vector<Entry> searchYouTube (const juce::File& ytDlp, const juce::String& phrase, int count,
                                         const std::atomic<bool>* cancel, juce::String& error)
{
    auto clean = phrase.removeCharacters ("\"").trim();
    if (clean.isEmpty())
        return {};
    return listEntries (ytDlp, "ytsearch" + juce::String (count) + ":\"" + clean + "\"", count, cancel, error);
}

} // namespace yoink
