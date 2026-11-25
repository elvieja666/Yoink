#pragma once
// Motor: corre indexar / buscar / recortar en un hilo aparte y avisa a la UI con ChangeBroadcaster.
#include <juce_events/juce_events.h>
#include "Captions.h"
#include "Search.h"
#include "Grab.h"

namespace yoink
{

class Engine : public juce::ChangeBroadcaster
{
public:
    Engine() : pool (1) {}

    ~Engine() override
    {
        cancel = true;
        pool.removeAllJobs (true, 15000);
    }

    struct Snapshot
    {
        juce::String status;
        bool busy = false;
        bool isError = false;
        std::vector<Hit> hits;
        juce::String lastQuery;
        int scannedVideos = 0, scannedLines = 0;
        LibraryStats stats;
    };

    Snapshot snapshot() const
    {
        const juce::ScopedLock sl (lock);
        return state;
    }

    /** Lo llama el procesador cuando un recorte termina bien (desde el hilo de mensajes). */
    std::function<void (const juce::File&)> onClipReady;

    void cancelCurrent() { cancel = true; }

    void refreshStats()
    {
        enqueue ([this]
        {
            auto s = libraryStats();
            const juce::ScopedLock sl (lock);
            state.stats = s;
        }, false);
    }

    void indexSource (const juce::String& rawUrl, int limit)
    {
        auto url = rawUrl.trim();
        if (url.isEmpty())
            return setStatus (yoink::T8 ("Pegá la URL de un video, playlist o canal."), true);

        enqueue ([this, url, limit]
        {
            ensureDirs();
            juce::String error;
            setStatus (yoink::T8 ("Preparando yt-dlp…"));
            auto ytDlp = ensureYtDlp (progressFn(), &cancel, error);
            if (ytDlp == juce::File())
                return setStatus (error, true);

            setStatus (yoink::T8 ("Leyendo la fuente…"));
            auto entries = listEntries (ytDlp, url, limit, &cancel, error);
            if (entries.empty())
                return setStatus (error.isNotEmpty() ? error : yoink::T8 ("No encontré videos en ") + url, true);

            int added = 0, skipped = 0;
            juce::StringArray addedIds, failed;
            for (size_t i = 0; i < entries.size(); ++i)
            {
                if (cancel) return setStatus ("Cancelado.", true);
                auto& e = entries[i];
                if (hasRecord (e.videoId)) { ++skipped; continue; }

                setStatus ("Indexando " + juce::String ((int) i + 1) + "/" + juce::String ((int) entries.size()) + ": "
                           + (e.title.isNotEmpty() ? e.title : e.videoId));
                auto t = fetchTranscript (e.videoId, ytDlp, &cancel);
                if (! t.ok())
                {
                    failed.add (e.videoId + " (" + t.error + ")");
                    continue;
                }
                auto& rec = t.record;
                if (rec.title.isEmpty()) rec.title = e.title;
                if (rec.channel.isEmpty()) rec.channel = e.channel;
                rec.indexedAt = juce::Time::getCurrentTime().toISO8601 (true);
                saveRecord (rec);
                addedIds.add (rec.videoId);
                ++added;
            }
            addSource (url, addedIds);
            searcher.invalidate();

            juce::String msg = "Indexados " + juce::String (added) + "/" + juce::String ((int) entries.size())
                             + " (" + juce::String (skipped) + " ya estaban, " + juce::String (failed.size()) + yoink::T8 (" sin subtítulos).");
            if (failed.size() > 0)
                msg << "  " << failed[0];
            auto s = libraryStats();
            {
                const juce::ScopedLock sl (lock);
                state.stats = s;
            }
            setStatus (msg, added == 0 && skipped == 0);
        });
    }

    void search (const juce::String& phrase, int limit = 30)
    {
        if (phrase.trim().isEmpty())
            return;
        enqueue ([this, phrase, limit]
        {
            setStatus (yoink::T8 ("Buscando…"));
            auto r = searcher.search (phrase, limit);
            {
                const juce::ScopedLock sl (lock);
                state.hits = r.hits;
                state.lastQuery = phrase;
                state.scannedVideos = r.scannedVideos;
                state.scannedLines = r.scannedLines;
            }
            if (r.scannedVideos == 0)
                setStatus (yoink::T8 ("El índice está vacío: primero indexá un video, playlist o canal."), true);
            else
                setStatus (juce::String ((int) r.hits.size()) + " resultados en " + juce::String (r.scannedVideos)
                           + " videos (" + juce::String (r.scannedLines) + yoink::T8 (" líneas)."), r.hits.empty());
        });
    }

    /** Busca en todo YouTube: candidatos del buscador, sus subtítulos en paralelo, y la frase en cada uno. */
    void searchYouTube (const juce::String& rawPhrase, int candidates)
    {
        auto phrase = rawPhrase.trim();
        if (phrase.isEmpty())
            return;
        enqueue ([this, phrase, candidates]
        {
            ensureDirs();
            juce::String error;
            auto ytDlp = ensureYtDlp (progressFn(), &cancel, error);
            if (ytDlp == juce::File())
                return setStatus (error, true);

            setStatus (yoink::T8 ("Buscando \"") + phrase + yoink::T8 ("\" en YouTube…"));
            auto entries = yoink::searchYouTube (ytDlp, phrase, candidates, &cancel, error);
            if (entries.empty())
                return setStatus (error.isNotEmpty() ? error : yoink::T8 ("YouTube no devolvió videos para esa frase."), true);

            {
                const juce::ScopedLock sl (lock);
                state.hits.clear();
                state.lastQuery = phrase;
            }
            sendChangeMessage();

            const int total = (int) entries.size();
            std::atomic<int> next { 0 }, done { 0 }, added { 0 }, noSubs { 0 };
            std::mutex recordsMutex;
            std::vector<Record> records;
            std::atomic<int> phraseHits { 0 };
            std::mutex publishMutex;

            auto publish = [&]
            {
                std::lock_guard<std::mutex> pg (publishMutex);
                std::vector<Record> copy;
                {
                    std::lock_guard<std::mutex> g (recordsMutex);
                    copy = records;
                }
                Searcher local;
                local.setRecords (std::move (copy));
                auto r = local.search (phrase, 60);
                std::vector<Hit> exact;
                for (auto& h : r.hits)
                    if (h.kind == "frase")
                        exact.push_back (h);
                const bool any = ! exact.empty();
                {
                    const juce::ScopedLock sl (lock);
                    state.hits = any ? exact : r.hits;
                    state.lastQuery = phrase;
                    state.scannedVideos = r.scannedVideos;
                    state.scannedLines = r.scannedLines;
                    phraseHits = (int) exact.size();
                }
            };

            auto worker = [&]
            {
                for (;;)
                {
                    const int i = next++;
                    if (i >= total || cancel)
                        return;
                    auto& e = entries[(size_t) i];
                    Record rec;
                    bool ok = loadRecord (e.videoId, rec);
                    if (! ok)
                    {
                        auto t = fetchTranscript (e.videoId, ytDlp, &cancel);
                        if (t.ok())
                        {
                            rec = t.record;
                            if (rec.title.isEmpty()) rec.title = e.title;
                            if (rec.channel.isEmpty()) rec.channel = e.channel;
                            rec.indexedAt = juce::Time::getCurrentTime().toISO8601 (true);
                            saveRecord (rec);
                            ++added;
                            ok = true;
                        }
                        else
                        {
                            ++noSubs;
                        }
                    }
                    if (ok)
                    {
                        std::lock_guard<std::mutex> g (recordsMutex);
                        records.push_back (rec);
                    }
                    const int d = ++done;
                    publish();
                    setStatus (yoink::T8 ("Revisando videos de YouTube: ") + juce::String (d) + "/" + juce::String (total)
                               + "  ·  " + juce::String (phraseHits.load()) + yoink::T8 (" con la frase exacta"));
                }
            };

            std::vector<std::thread> workers;
            for (int t = 0; t < 4; ++t)
                workers.emplace_back (worker);
            for (auto& t : workers)
                t.join();

            searcher.invalidate();
            auto s = libraryStats();
            {
                const juce::ScopedLock sl (lock);
                state.stats = s;
            }
            if (cancel)
                return setStatus ("Cancelado.", true);

            juce::String msg;
            if (phraseHits > 0)
                msg << juce::String (phraseHits.load()) << yoink::T8 (phraseHits.load() == 1 ? " momento con la frase exacta en " : " momentos con la frase exacta en ") << juce::String (total)
                    << " videos de YouTube.";
            else
                msg << yoink::T8 ("Ningún video de los ") << juce::String (total)
                    << yoink::T8 (" revisados dice la frase exacta; muestro coincidencias parciales. Probá con menos palabras o más candidatos.");
            msg << "  (" << juce::String (noSubs.load()) << yoink::T8 (" sin subtítulos, ") << juce::String (added.load())
                << yoink::T8 (" nuevos en tu índice)");
            setStatus (msg, phraseHits.load() == 0);
        });
    }

    void grab (const GrabRequest& request)
    {
        enqueue ([this, request]
        {
            auto r = grabClip (request, progressFn(), &cancel);
            if (! r.ok())
                return setStatus (r.error, true);
            setStatus ("Listo: " + r.file.getFileName() + yoink::T8 ("  — arrastralo al DAW o tocalo por MIDI."));
            auto file = r.file;
            juce::MessageManager::callAsync ([this, file, alive = juce::WeakReference<Engine> (this)]
            {
                if (alive != nullptr && onClipReady)
                    onClipReady (file);
            });
            auto s = libraryStats();
            const juce::ScopedLock sl (lock);
            state.stats = s;
        });
    }

    void updateYtDlp()
    {
        enqueue ([this]
        {
            setStatus (yoink::T8 ("Actualizando yt-dlp…"));
            setStatus ("yt-dlp: " + yoink::updateYtDlp (&cancel));
        });
    }

private:
    void enqueue (std::function<void()> job, bool markBusy = true)
    {
        pool.addJob ([this, job, markBusy]
        {
            cancel = false;
            if (markBusy) setBusy (true);
            job();
            if (markBusy) setBusy (false);
            sendChangeMessage();
        });
    }

    ProgressFn progressFn() { return [this] (const juce::String& s) { setStatus (s); }; }

    void setStatus (const juce::String& s, bool error = false)
    {
        {
            const juce::ScopedLock sl (lock);
            state.status = s;
            state.isError = error;
        }
        sendChangeMessage();
    }

    void setBusy (bool b)
    {
        {
            const juce::ScopedLock sl (lock);
            state.busy = b;
        }
        sendChangeMessage();
    }

    juce::ThreadPool pool;
    std::atomic<bool> cancel { false };
    juce::CriticalSection lock;
    Snapshot state;
    Searcher searcher;

    JUCE_DECLARE_WEAK_REFERENCEABLE (Engine)
};

} // namespace yoink
