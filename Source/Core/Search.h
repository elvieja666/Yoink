#pragma once
// Búsqueda: BM25 por línea de subtítulo + pasada de frase exacta (palabras contiguas).
// Sin tildes ni mayúsculas ni puntuación. El corpus se cachea por huella del índice.
#include "Store.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <mutex>
#include <algorithm>
#include <cmath>

namespace yoink
{

using U32 = std::u32string;

//==============================================================================
// Normalización (posición a posición: un caracter de entrada -> uno de salida,
// salvo las marcas combinantes, que se descartan)

inline bool isCJK (char32_t c)
{
    return (c >= 0x3040 && c <= 0x30FF) || (c >= 0x3400 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF);
}

inline char32_t lowerFold (char32_t c)
{
    if (c < 0x80)
        return (c >= 'A' && c <= 'Z') ? c + 32 : c;

    if (c >= 0xC0 && c <= 0xFF)
    {
        static const char* fold = "aaaaaaaceeeeiiii" "dnooooo*ouuuuy*s" "aaaaaaaceeeeiiii" "dnooooo*ouuuuy*y";
        auto f = fold[c - 0xC0];
        if (f != '*') return (char32_t) f;
        if (c == 0xDE) return 0xFE;          // Þ -> þ
        return c;                            // × ÷ þ
    }

    if (c >= 0x100 && c <= 0x17F)
    {
        static const char* fold = "aaaaaacccccccc" "dddd" "eeeeeeeeee" "gggggggg" "hhhh" "iiiiiiiiii" "**" "jj" "kkk"
                                  "llllllllll" "nnnnnnnnn" "oooooo" "**" "rrrrrr" "ssssssss" "tttttt" "uuuuuuuuuuuu"
                                  "ww" "yyy" "zzzzzz" "s";
        auto f = fold[c - 0x100];
        if (f != '*') return (char32_t) f;
        return (c % 2 == 0) ? c + 1 : c;     // Ĳ Œ -> minúscula
    }

    if (c >= 0x391 && c <= 0x3A9) return c + 32;   // griego
    if (c >= 0x410 && c <= 0x42F) return c + 32;   // cirílico
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}

inline bool isCombining (char32_t c) { return c >= 0x300 && c <= 0x36F; }

/** ¿Letra o dígito? (lo que no, pasa a espacio) */
inline bool isWordChar (char32_t c)
{
    if (c < 0x80)
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    if (c < 0xC0) return c == 0xAA || c == 0xB5 || c == 0xBA;
    if (c == 0xD7 || c == 0xF7) return false;
    if (c >= 0x2000 && c <= 0x2BFF) return false;  // puntuación general, símbolos, flechas
    if (c >= 0x3000 && c <= 0x303F) return false;  // puntuación CJK
    if (c >= 0xFE30 && c <= 0xFE4F) return false;
    if (c >= 0xFF00 && c <= 0xFF0F) return false;
    if (c >= 0xFF1A && c <= 0xFF20) return false;
    if (c >= 0x1F000) return false;                // emoji
    return true;
}

inline U32 normalize (const juce::String& text)
{
    U32 out;
    out.reserve ((size_t) text.length());
    for (auto p = text.getCharPointer(); ! p.isEmpty(); ++p)
    {
        auto c = (char32_t) *p;
        if (isCombining (c))
            continue;
        out.push_back (isWordChar (c) ? lowerFold (c) : U' ');
    }
    return out;
}

/** Palabras de un texto normalizado, con su posición. */
struct Word
{
    U32 text;
    size_t pos;
};

inline std::vector<Word> words (const U32& norm)
{
    std::vector<Word> out;
    size_t i = 0;
    while (i < norm.size())
    {
        while (i < norm.size() && norm[i] == U' ') ++i;
        auto start = i;
        while (i < norm.size() && norm[i] != U' ') ++i;
        if (i > start)
            out.push_back ({ norm.substr (start, i - start), start });
    }
    return out;
}

/** Tokens para BM25; las corridas CJK aportan caracteres sueltos y bigramas. */
inline std::vector<U32> tokenize (const juce::String& text)
{
    std::vector<U32> tokens;
    for (auto& w : words (normalize (text)))
    {
        bool cjk = std::any_of (w.text.begin(), w.text.end(), isCJK);
        if (! cjk)
        {
            tokens.push_back (w.text);
            continue;
        }
        for (size_t i = 0; i < w.text.size(); ++i)
            tokens.push_back (w.text.substr (i, 1));
        for (size_t i = 0; i + 1 < w.text.size(); ++i)
            tokens.push_back (w.text.substr (i, 2));
    }
    return tokens;
}

//==============================================================================

struct Hit
{
    juce::String videoId, title, channel, text, kind;
    int start = 0;
    double score = 0;
    juce::String timestamp() const { return stamp (start); }
    juce::String url() const { return "https://youtu.be/" + videoId + "?t=" + juce::String (start); }
};

struct SearchResult
{
    std::vector<Hit> hits;
    int scannedVideos = 0, scannedLines = 0;
};

class Searcher
{
public:
    static constexpr double K1 = 1.2, B = 0.75;

    /** Para tests: cargar registros a mano en vez de leer el disco. */
    void setRecords (std::vector<Record> records)
    {
        std::lock_guard<std::mutex> lock (mutex);
        build (std::move (records));
        signature = "manual";
        manual = true;
    }

    void invalidate()
    {
        std::lock_guard<std::mutex> lock (mutex);
        signature.clear();
        manual = false;
    }

    SearchResult search (const juce::String& phrase, int limit)
    {
        std::lock_guard<std::mutex> lock (mutex);
        if (! manual)
        {
            auto sig = indexSignature();
            if (sig != signature)
            {
                build (listRecords());
                signature = sig;
            }
        }

        SearchResult result;
        result.scannedVideos = (int) videos.size();
        result.scannedLines = totalDocs;
        if (totalDocs == 0)
            return result;

        std::vector<U32> queryTokens;
        {
            std::unordered_set<U32> seen;
            for (auto& t : tokenize (phrase))
                if (seen.insert (t).second)
                    queryTokens.push_back (t);
        }

        std::map<std::pair<int, int>, Scored> hits;
        auto push = [&] (int v, int l, double score, const char* kind)
        {
            auto key = std::make_pair (v, videos[(size_t) v].record.lines[(size_t) l].t / 4);
            auto it = hits.find (key);
            if (it != hits.end() && it->second.score >= score)
                return;
            hits[key] = { v, l, score, kind };
        };

        // 1) frase exacta: palabras contiguas (o subcadena si hay CJK)
        auto qnorm = normalize (phrase);
        auto qwords = words (qnorm);
        if (! qwords.empty())
        {
            bool cjk = std::any_of (qnorm.begin(), qnorm.end(), isCJK);
            U32 collapsed;
            for (auto& w : qwords) { if (! collapsed.empty()) collapsed += U' '; collapsed += w.text; }

            for (int v = 0; v < (int) videos.size(); ++v)
            {
                auto& video = videos[(size_t) v];
                if (cjk)
                {
                    for (auto at = video.joined.find (collapsed); at != U32::npos; at = video.joined.find (collapsed, at + 1))
                        push (v, lineAt (video.starts, at), 1000.0 + (double) queryTokens.size() * 10.0, "frase");
                    continue;
                }
                auto& ws = video.words;
                for (size_t i = 0; i + qwords.size() <= ws.size(); ++i)
                {
                    bool match = true;
                    for (size_t k = 0; k < qwords.size() && match; ++k)
                        match = ws[i + k].text == qwords[k].text;
                    if (match)
                        push (v, lineAt (video.starts, ws[i].pos), 1000.0 + (double) queryTokens.size() * 10.0, "frase");
                }
            }
        }

        // 2) BM25 palabra por palabra
        const double N = totalDocs;
        for (auto& term : queryTokens)
        {
            auto dfIt = df.find (term);
            if (dfIt == df.end())
                continue;
            const double d = dfIt->second;
            const double idf = std::log (1.0 + (N - d + 0.5) / (d + 0.5));
            for (auto& [v, l] : postings[term])
            {
                auto& doc = videos[(size_t) v].docs[(size_t) l];
                const double tf = doc.tf.at (term);
                const double score = idf * (tf * (K1 + 1)) / (tf + K1 * (1 - B + B * doc.len / avgdl));
                push (v, l, score, "palabras");
            }
        }

        std::vector<Scored> ordered;
        for (auto& [_, s] : hits)
            ordered.push_back (s);
        std::stable_sort (ordered.begin(), ordered.end(), [] (const Scored& a, const Scored& b) { return a.score > b.score; });
        if ((int) ordered.size() > limit)
            ordered.resize ((size_t) limit);

        for (auto& s : ordered)
        {
            auto& rec = videos[(size_t) s.video].record;
            auto& lines = rec.lines;
            auto l = (size_t) s.line;
            juce::String text = (l > 0 ? lines[l - 1].text + " " : juce::String()) + lines[l].text
                              + (l + 1 < lines.size() ? " " + lines[l + 1].text : juce::String());
            Hit h;
            h.videoId = rec.videoId;
            h.title = rec.title.isNotEmpty() ? rec.title : rec.videoId;
            h.channel = rec.channel;
            h.start = lines[l].t;
            h.text = text.trim().substring (0, 240);
            h.kind = s.kind;
            h.score = std::round (s.score * 100.0) / 100.0;
            result.hits.push_back (h);
        }
        return result;
    }

    static int lineAt (const std::vector<size_t>& starts, size_t offset)
    {
        auto it = std::upper_bound (starts.begin(), starts.end(), offset);
        return (int) juce::jmax<std::ptrdiff_t> (0, (it - starts.begin()) - 1);
    }

private:
    struct Doc
    {
        std::unordered_map<U32, int> tf;
        int len = 0;
    };
    struct Video
    {
        Record record;
        U32 joined;
        std::vector<size_t> starts;
        std::vector<Word> words;
        std::vector<Doc> docs;
    };
    struct Scored
    {
        int video = 0, line = 0;
        double score = 0;
        const char* kind = "";
    };

    void build (std::vector<Record> records)
    {
        videos.clear();
        df.clear();
        postings.clear();
        totalDocs = 0;
        long long totalTokens = 0;
        for (auto& rec : records)
        {
            Video video;
            for (auto& line : rec.lines)
            {
                video.starts.push_back (video.joined.size());
                video.joined += normalize (line.text);
                video.joined += U' ';
            }
            video.words = words (video.joined);
            const int v = (int) videos.size();
            for (size_t l = 0; l < rec.lines.size(); ++l)
            {
                Doc doc;
                auto tokens = tokenize (rec.lines[l].text);
                for (auto& t : tokens)
                    ++doc.tf[t];
                for (auto& [t, _] : doc.tf)
                {
                    ++df[t];
                    postings[t].push_back ({ v, (int) l });
                }
                doc.len = (int) tokens.size();
                totalTokens += doc.len;
                ++totalDocs;
                video.docs.push_back (std::move (doc));
            }
            video.record = std::move (rec);
            videos.push_back (std::move (video));
        }
        avgdl = totalDocs > 0 ? juce::jmax (1.0, (double) totalTokens / totalDocs) : 1.0;
    }

    std::mutex mutex;
    juce::String signature;
    bool manual = false;
    std::vector<Video> videos;
    std::unordered_map<U32, int> df;
    std::unordered_map<U32, std::vector<std::pair<int, int>>> postings;
    int totalDocs = 0;
    double avgdl = 1.0;
};

} // namespace yoink
