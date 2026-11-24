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
