// Tests del motor sin red: índice en disco, búsqueda, normalización, parseo de URLs y json3.
#include <juce_core/juce_core.h>
#include <iostream>

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { std::cerr << "FALLA " << __LINE__ << ": " #cond "\n"; ++failures; } } while (0)

static std::string utf8 (const std::u32string& s)
{
    juce::String out;
    for (auto c : s) out += juce::String::charToString ((juce::juce_wchar) c);
    return out.toStdString();
}

#include "Core/Search.h"
#include "Core/Captions.h"
#include "Core/Grab.h"

using namespace yoink;

static Record makeRecord (const char* id, const char* title, std::vector<Line> lines)
{
    Record r;
    r.videoId = id;
    r.title = title;
    r.channel = "Canal";
    r.lang = "es";
    r.indexedAt = juce::Time::getCurrentTime().toISO8601 (true);
    r.lines = std::move (lines);
    return r;
}

int main()
{

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("yoink-test", "");
    dir.createDirectory();
    setenv ("YOINK_DIR", dir.getFullPathName().toRawUTF8(), 1);
    setenv ("YOINK_CLIPS", dir.getChildFile ("clips").getFullPathName().toRawUTF8(), 1);
    setenv ("DSH_HOME", dir.getChildFile ("no-dsh").getFullPathName().toRawUTF8(), 1);
    ensureDirs();

    // --- índice en disco
    CHECK (saveRecord (makeRecord ("aaaaaaaaaaa", "Entrevista", {
        { 4, juce::String::fromUTF8 ("Bueno, yo creo que") },
        { 8, juce::String::fromUTF8 ("nunca voy a rendirme, \xc2\xbfme entend\xc3\xa9s?") },
        { 14, "Eso es lo que pienso." } })));
    CHECK (saveRecord (makeRecord ("bbbbbbbbbbb", "Charla", {
        { 30, "Hay que rendirse nunca." },
        { 60, "Otra frase distinta." } })));
    CHECK (hasRecord ("aaaaaaaaaaa"));
    CHECK (! hasRecord ("ccccccccccc"));
    CHECK (listRecords().size() == 2);

    auto st = libraryStats();
    CHECK (st.videos == 2 && st.lines == 5);

    // ida y vuelta del JSON conserva acentos
    {
        Record back;
        CHECK (Record::fromVar (readJson (recordFile ("aaaaaaaaaaa")), back));
        CHECK (back.lines.size() == 3 && back.lines[1].text.contains (juce::String::fromUTF8 ("entend\xc3\xa9s")));
    }

    // --- búsqueda (lee el disco)
    Searcher searcher;
    auto phrase = searcher.search ("nunca voy a rendirme", 5);
    CHECK (! phrase.hits.empty());
    if (! phrase.hits.empty())
    {
        CHECK (phrase.hits[0].videoId == "aaaaaaaaaaa");
        CHECK (phrase.hits[0].kind == "frase");
        CHECK (phrase.hits[0].start == 8);
        CHECK (phrase.hits[0].timestamp() == "00:08");
    }
    CHECK (phrase.scannedVideos == 2 && phrase.scannedLines == 5);

    auto loose = searcher.search ("rendirme", 5);
    CHECK (loose.hits.size() == 1);

    auto accented = searcher.search ("ME ENTENDES", 5);
    CHECK (! accented.hits.empty() && accented.hits[0].kind == "frase");

    // frase que cruza dos líneas: "creo que nunca"
    auto across = searcher.search ("creo que nunca", 5);
    CHECK (! across.hits.empty() && across.hits[0].kind == "frase" && across.hits[0].start == 4);

    // sin falsos positivos dentro de palabras
    auto partial = searcher.search ("rendir", 5);
    for (auto& h : partial.hits) CHECK (h.kind != "frase");

    // el índice cambia -> el corpus se reconstruye
    CHECK (saveRecord (makeRecord ("ccccccccccc", "Nuevo", { { 3, "no puede ser" } })));
    CHECK (searcher.search ("no puede ser", 5).scannedVideos == 3);

    // --- normalización
    CHECK (utf8 (normalize (juce::String::fromUTF8 ("\xc2\xbfMe entend\xc3\xa9s?"))) == " me entendes ");
    CHECK (utf8 (normalize (juce::String::fromUTF8 ("\xc3\x91" "and\xc3\xba \xc3\x87" "A"))) == "nandu ca");
    {
        auto t = tokenize ("Hola, mundo");
        CHECK (t.size() == 2 && utf8 (t[0]) == "hola");
        auto cjk = tokenize (juce::String::fromUTF8 ("\xe6\x97\xa5\xe6\x9c\xac"));   // 日本
        CHECK (cjk.size() == 3);
    }

    // --- URLs
    CHECK (parseVideoId ("https://youtu.be/dQw4w9WgXcQ?t=3") == "dQw4w9WgXcQ");
    CHECK (parseVideoId ("https://www.youtube.com/watch?v=dQw4w9WgXcQ&list=PL1") == "dQw4w9WgXcQ");
    CHECK (parseVideoId ("https://www.youtube.com/watch?feature=x&v=dQw4w9WgXcQ") == "dQw4w9WgXcQ");
    CHECK (parseVideoId ("https://www.youtube.com/shorts/dQw4w9WgXcQ") == "dQw4w9WgXcQ");
    CHECK (parseVideoId ("dQw4w9WgXcQ") == "dQw4w9WgXcQ");
    CHECK (parseVideoId ("https://www.youtube.com/@canal").isEmpty());
    CHECK (normalizeSourceUrl ("https://www.youtube.com/@canal/") == "https://www.youtube.com/@canal/videos");
    CHECK (normalizeSourceUrl ("https://www.youtube.com/@canal/streams") == "https://www.youtube.com/@canal/streams");
    CHECK (normalizeSourceUrl ("https://www.youtube.com/playlist?list=PL1") == "https://www.youtube.com/playlist?list=PL1");
    CHECK (sourceKind ("https://www.youtube.com/playlist?list=PL1") == "playlist");
    CHECK (sourceKind ("https://youtu.be/dQw4w9WgXcQ") == "video");

    // --- tiempos y nombres
    CHECK (stamp (83.4) == "01:23");
    CHECK (clock (83.5) == "00:01:23.500");
    CHECK (clock (3725.25) == "01:02:05.250");
    CHECK (slug (juce::String::fromUTF8 ("\xc2\xa1No puede SER! \xc3\xb1" "and\xc3\xba")) == "no-puede-ser-nandu");
    CHECK (slug ("???") == "yoink");

    // --- json3 y pistas
    {
        auto payload = juce::JSON::parse (R"({"events":[{"tStartMs":0},{"tStartMs":1500,"segs":[{"utf8":"hola "},{"utf8":"mundo"}]},
                                            {"tStartMs":2000,"segs":[{"utf8":"hola mundo"}]},{"tStartMs":4400,"segs":[{"utf8":"\nchau"}]}]})");
        auto lines = linesFromJson3 (payload);
        CHECK (lines.size() == 2);
        if (lines.size() == 2)
        {
            CHECK (lines[0].t == 2 && lines[0].text == "hola mundo");
            CHECK (lines[1].t == 4 && lines[1].text == "chau");
        }

        auto tracks = juce::JSON::parse (R"([{"languageCode":"en","kind":"asr","baseUrl":"a"},
                                            {"languageCode":"es","kind":"asr","baseUrl":"b"},
                                            {"languageCode":"es-419","baseUrl":"c"}])");
        CHECK (pickTrack (tracks, { "es", "en" })["baseUrl"].toString() == "c");
        CHECK (pickTrack (tracks, { "en" })["baseUrl"].toString() == "a");

        auto html = juce::String (R"(var x = 1; var ytInitialPlayerResponse = {"a":{"b":"}{"},"c":2};</script>)");
        auto obj = extractObject (html, "ytInitialPlayerResponse");
        CHECK ((int) obj["c"] == 2 && obj["a"]["b"].toString() == "}{");
        CHECK (matchAfter (R"("INNERTUBE_API_KEY":"KEY123","x":1)", "INNERTUBE_API_KEY") == "KEY123");
    }

    // --- fuentes
    addSource ("https://www.youtube.com/@canal", { "aaaaaaaaaaa" });
    addSource ("https://www.youtube.com/@canal", { "aaaaaaaaaaa" });
    CHECK (libraryStats().sources == 1);

    // --- procesos
    {
        auto r = run ({ "/bin/sh", "-c", "echo hola; echo ERROR: algo >&2; exit 3" }, 5000);
        CHECK (r.started && r.code == 3 && r.lastLine() == "ERROR: algo");
        auto slow = run ({ "/bin/sleep", "5" }, 300);
        CHECK (slow.timedOut);
    }

    dir.deleteRecursively();
    if (failures == 0)
        std::cout << "ok: todos los tests del motor pasaron\n";
    return failures == 0 ? 0 : 1;
}
