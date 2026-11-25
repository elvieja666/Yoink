#pragma once
// Índice en disco: un JSON por video (mismo formato que la versión DeepSeek, así se puede reutilizar).
#include "Util.h"
#include <vector>

namespace yoink
{

struct Line
{
    int t = 0;               // segundo de inicio
    juce::String text;
};

struct Record
{
    juce::String videoId, title, channel, lang, source, indexedAt;
    bool asr = false;
    double duration = 0;
    std::vector<Line> lines;

    juce::var toVar() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("videoId", videoId);
        o->setProperty ("title", title);
        o->setProperty ("channel", channel);
        o->setProperty ("duration", duration > 0 ? juce::var (duration) : juce::var());
        o->setProperty ("lang", lang);
        o->setProperty ("asr", asr);
        o->setProperty ("source", source);
        o->setProperty ("indexedAt", indexedAt);
        juce::Array<juce::var> arr;
        for (auto& l : lines)
        {
            auto* lo = new juce::DynamicObject();
            lo->setProperty ("t", l.t);
            lo->setProperty ("text", l.text);
            arr.add (juce::var (lo));
        }
        o->setProperty ("lines", arr);
        return juce::var (o);
    }

    static bool fromVar (const juce::var& v, Record& r)
    {
        if (! v.isObject())
            return false;
        r.videoId = v["videoId"].toString();
        r.title = v["title"].toString();
        r.channel = v["channel"].toString();
        r.lang = v["lang"].toString();
        r.source = v["source"].toString();
        r.indexedAt = v["indexedAt"].toString();
        r.asr = (bool) v["asr"];
        r.duration = v["duration"].isVoid() ? 0.0 : (double) v["duration"];
        r.lines.clear();
        if (auto* arr = v["lines"].getArray())
            for (auto& l : *arr)
                r.lines.push_back ({ (int) l["t"], l["text"].toString() });
        return isVideoId (r.videoId) && ! r.lines.empty();
    }
};

/** Escritura atómica: un corte a mitad nunca corrompe el archivo. */
inline bool writeJson (const juce::File& file, const juce::var& value)
{
    file.getParentDirectory().createDirectory();
    juce::TemporaryFile tmp (file);
    if (! tmp.getFile().replaceWithText (juce::JSON::toString (value) + "\n", false, false, "\n"))
        return false;
    return tmp.overwriteTargetFileWithTemporary();
}

inline juce::var readJson (const juce::File& file)
{
    if (! file.existsAsFile())
        return {};
    return juce::JSON::parse (file.loadFileAsString());
}

inline juce::File recordFile (const juce::String& videoId) { return indexDir().getChildFile (videoId + ".json"); }

inline bool hasRecord (const juce::String& videoId)
{
    return recordFile (videoId).existsAsFile() || legacyIndexDir().getChildFile (videoId + ".json").existsAsFile();
}

inline bool loadRecord (const juce::String& videoId, Record& r)
{
    for (auto dir : { indexDir(), legacyIndexDir() })
        if (Record::fromVar (readJson (dir.getChildFile (videoId + ".json")), r))
            return true;
    return false;
}

inline bool saveRecord (const Record& r) { return writeJson (recordFile (r.videoId), r.toVar()); }

inline juce::Array<juce::File> indexFiles()
{
    juce::Array<juce::File> files;
    juce::StringArray seen;
    for (auto dir : { indexDir(), legacyIndexDir() })
        if (dir.isDirectory())
            for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.json"))
                if (! seen.contains (f.getFileName()))
                {
                    seen.add (f.getFileName());
                    files.add (f);
                }
    files.sort();
    return files;
}

inline std::vector<Record> listRecords()
{
    std::vector<Record> out;
    for (auto& f : indexFiles())
    {
        Record r;
        if (Record::fromVar (readJson (f), r))
            out.push_back (std::move (r));
    }
    return out;
}

/** Huella barata del índice: se reconstruye el corpus en memoria solo si cambia. */
inline juce::String indexSignature()
{
    juce::String sig;
    for (auto& f : indexFiles())
        sig << f.getFullPathName() << ':' << f.getSize() << ':' << f.getLastModificationTime().toMilliseconds() << '|';
    return sig.isEmpty() ? juce::String ("empty") : sig;
}

//==============================================================================
// Registro de fuentes

inline juce::var readSources()
{
    auto v = readJson (sourcesFile());
    if (v.isObject() && v["sources"].isArray())
        return v;
    auto* o = new juce::DynamicObject();
    o->setProperty ("sources", juce::Array<juce::var>());
    return juce::var (o);
}

inline void addSource (const juce::String& url, const juce::StringArray& videoIds)
{
    auto reg = readSources();
    auto* arr = reg["sources"].getArray();
    for (auto& s : *arr)
        if (s["url"].toString() == url)
            return;
    auto* o = new juce::DynamicObject();
    o->setProperty ("url", url);
    o->setProperty ("kind", sourceKind (url));
    o->setProperty ("addedAt", juce::Time::getCurrentTime().toISO8601 (true));
    juce::Array<juce::var> ids;
    for (auto& id : videoIds)
        ids.add (id);
    o->setProperty ("videos", ids);
    arr->add (juce::var (o));
    writeJson (sourcesFile(), reg);
}

struct LibraryStats
{
    int videos = 0, lines = 0, clips = 0, sources = 0;
};

inline LibraryStats libraryStats()
{
    LibraryStats s;
    for (auto& r : listRecords())
    {
        ++s.videos;
        s.lines += (int) r.lines.size();
    }
    if (clipsDir().isDirectory())
        s.clips = clipsDir().getNumberOfChildFiles (juce::File::findFiles, "*.wav");
    const auto reg = readSources();   // mantener vivo el var: getArray() apunta adentro
    if (auto* arr = reg["sources"].getArray())
        s.sources = arr->size();
    return s;
}

} // namespace yoink
