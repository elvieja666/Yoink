#include "PluginEditor.h"

using namespace yoink;

//==============================================================================
ClipView::ClipView (YoinkProcessor& p)
    : processor (p), thumbnail (512, p.formats, cache)
{
    thumbnail.addChangeListener (this);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

ClipView::~ClipView()
{
    thumbnail.removeChangeListener (this);
}

void ClipView::setFile (const juce::File& f)
{
    if (f == file)
        return;
    file = f;
    if (file.existsAsFile())
        thumbnail.setSource (new juce::FileInputSource (file));
    else
        thumbnail.clear();
    repaint();
}

void ClipView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (ui::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (ui::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    auto inner = getLocalBounds().reduced (10);
    if (! file.existsAsFile() || thumbnail.getTotalLength() <= 0)
    {
        g.setColour (ui::dim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (yoink::T8 ("Todavía no hay clip. Buscá una frase y apretá Yoink!"), inner, juce::Justification::centred);
        return;
    }

    auto header = inner.removeFromTop (18);
    g.setColour (ui::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (file.getFileName(), header, juce::Justification::centredLeft, true);
    g.setColour (ui::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (juce::String (thumbnail.getTotalLength(), 2) + yoink::T8 (" s   ·   arrastrá la onda al DAW   ·   MIDI: Do3 = original"),
                header, juce::Justification::centredRight, true);

    inner.removeFromTop (4);
    g.setColour (ui::accent.withAlpha (dragging ? 0.6f : 0.9f));
    thumbnail.drawChannels (g, inner, 0.0, thumbnail.getTotalLength(), 1.0f);
}

void ClipView::mouseDown (const juce::MouseEvent&) { dragging = false; }

void ClipView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || ! file.existsAsFile() || e.getDistanceFromDragStart() < 6)
        return;
    dragging = true;
    repaint();
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                [safe = juce::Component::SafePointer<ClipView> (this)]
                                                                {
                                                                    if (safe != nullptr)
                                                                    {
                                                                        safe->dragging = false;
                                                                        safe->repaint();
                                                                    }
                                                                });
}

//==============================================================================
static void styleEdit (juce::TextEditor& e, const juce::String& placeholder)
{
    e.setTextToShowWhenEmpty (placeholder, ui::dim);
    e.setFont (juce::FontOptions (15.0f));
    e.setIndents (8, 0);
    e.setJustification (juce::Justification::centredLeft);
    e.setColour (juce::TextEditor::backgroundColourId, ui::panel);
    e.setColour (juce::TextEditor::outlineColourId, ui::line);
    e.setColour (juce::TextEditor::focusedOutlineColourId, ui::accent);
    e.setColour (juce::TextEditor::textColourId, ui::text);
}

static void styleLabel (juce::Label& l, const juce::String& t, float size = 13.0f, juce::Colour c = ui::dim)
{
    l.setText (t, juce::dontSendNotification);
    l.setFont (juce::FontOptions (size));
    l.setColour (juce::Label::textColourId, c);
}

static void styleKnob (juce::Slider& s, juce::Label& l, const juce::String& name)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    s.setColour (juce::Slider::rotarySliderFillColourId, ui::accent);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    styleLabel (l, name, 12.0f);
    l.setJustificationType (juce::Justification::centred);
}

YoinkEditor::YoinkEditor (YoinkProcessor& p)
    : AudioProcessorEditor (p), processor (p), clipView (p)
{
    lnf.setColourScheme ({ ui::bg, ui::panel, ui::panel, ui::line, ui::text, ui::accent.darker (0.2f),
                           juce::Colours::black, ui::accent, ui::text });
    lnf.setColour (juce::TextButton::buttonColourId, ui::panel);
    lnf.setColour (juce::TextButton::textColourOffId, ui::text);
    lnf.setColour (juce::ComboBox::backgroundColourId, ui::panel);
    lnf.setColour (juce::ComboBox::outlineColourId, ui::line);
    lnf.setColour (juce::ListBox::backgroundColourId, ui::panel);
    setLookAndFeel (&lnf);

    styleLabel (title, "YOINK", 26.0f, ui::accent);
    title.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    styleLabel (stats, "", 12.0f);
    stats.setJustificationType (juce::Justification::centredRight);
    styleLabel (sourceLabel, "Fuente");
    styleLabel (queryLabel, "Frase");
    styleLabel (lengthLabel, yoink::T8 ("Duración (s)"));
    styleLabel (offsetLabel, "Desfase (s)");
    styleLabel (statusLabel, "", 13.0f, ui::text);
    styleLabel (hint, "Doble clic o Enter en un resultado = Yoink", 12.0f);

    styleEdit (sourceEdit, "URL de video, playlist o canal de YouTube");
    styleEdit (queryEdit, "Lo que dice alguien, p. ej. \"no puede ser\"");
    sourceEdit.setText (processor.lastSource, false);
    queryEdit.setText (processor.lastQuery, false);
    sourceEdit.onReturnKey = [this] { doIndex(); };
    queryEdit.onReturnKey = [this] { doSearch(); };

    for (int n : { 1, 5, 10, 20, 40 })
        limitBox.addItem ("hasta " + juce::String (n), n);
    limitBox.setSelectedId (10, juce::dontSendNotification);
    youtubeToggle.setButtonText ("Todo YouTube");
    youtubeToggle.setToggleState (processor.searchAllYouTube, juce::dontSendNotification);
    youtubeToggle.setColour (juce::ToggleButton::tickColourId, ui::accent);
    youtubeToggle.setTooltip (yoink::T8 ("Prendido: busca en YouTube y revisa los subtítulos de los videos que encuentra. Apagado: solo en tu índice."));
    youtubeToggle.onClick = [this]
    {
        processor.searchAllYouTube = youtubeToggle.getToggleState();
        candBox.setEnabled (processor.searchAllYouTube);
    };
    for (int n : { 20, 40, 80 })
        candBox.addItem (juce::String (n) + " videos", n);
    candBox.setSelectedId (processor.youtubeCandidates, juce::dontSendNotification);
    if (candBox.getSelectedId() == 0) candBox.setSelectedId (40, juce::dontSendNotification);
    candBox.setTooltip (yoink::T8 ("Cuántos videos del buscador de YouTube revisar (más = más lento, más chances)"));
    candBox.onChange = [this] { processor.youtubeCandidates = candBox.getSelectedId(); };
    candBox.setEnabled (processor.searchAllYouTube);

    limitBox.setTooltip (yoink::T8 ("Cuántos videos tomar de una playlist o canal"));

    indexBtn.onClick = [this] { doIndex(); };
    searchBtn.onClick = [this] { doSearch(); };
    grabBtn.onClick = [this] { grab (list.getSelectedRow()); };
    grabBtn.setColour (juce::TextButton::buttonColourId, ui::accent);
    grabBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    ytBtn.onClick = [this]
    {
        auto row = list.getSelectedRow();
        if (juce::isPositiveAndBelow (row, (int) hits.size()))
            juce::URL (hits[(size_t) row].url()).launchInDefaultBrowser();
    };
    playBtn.onClick = [this] { processor.previewStart(); };
    stopBtn.onClick = [this] { processor.previewStop(); };
    revealBtn.onClick = [this] { if (processor.getClip().existsAsFile()) processor.getClip().revealToUser(); };
    folderBtn.onClick = [] { clipsDir().createDirectory(); clipsDir().startAsProcess(); };
    cancelBtn.onClick = [this] { processor.engine.cancelCurrent(); };
    updateBtn.onClick = [this] { processor.engine.updateYtDlp(); };
   #if ! JUCE_MAC
    revealBtn.setButtonText ("Mostrar archivo");
   #endif

    lengthSlider.setRange (0.5, 30.0, 0.5);
    lengthSlider.setValue (processor.clipLength, juce::dontSendNotification);
    lengthSlider.onValueChange = [this] { processor.clipLength = lengthSlider.getValue(); };
    offsetSlider.setRange (-3.0, 3.0, 0.1);
    offsetSlider.setValue (processor.clipOffset, juce::dontSendNotification);
    offsetSlider.onValueChange = [this] { processor.clipOffset = offsetSlider.getValue(); };
    offsetSlider.setTooltip (yoink::T8 ("Los subtítulos marcan el segundo redondeado: arrancá un poco antes"));
    for (auto* s : { &lengthSlider, &offsetSlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        s->setColour (juce::Slider::thumbColourId, ui::accent);
        s->setColour (juce::Slider::trackColourId, ui::accent.withAlpha (0.5f));
    }

    styleKnob (gainKnob, gainLabel, "Ganancia");
    styleKnob (attackKnob, attackLabel, "Ataque");
    styleKnob (releaseKnob, releaseLabel, "Release");
    gainAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.params, "gain", gainKnob);
    attackAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.params, "attack", attackKnob);
    releaseAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.params, "release", releaseKnob);

    list.setModel (this);
    list.setRowHeight (52);
    list.setColour (juce::ListBox::outlineColourId, ui::line);
    list.setOutlineThickness (1);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &title, &stats, &sourceLabel, &queryLabel, &sourceEdit, &limitBox, &indexBtn, &queryEdit, &searchBtn, &youtubeToggle, &candBox,
             &list, &hint, &lengthLabel, &lengthSlider, &offsetLabel, &offsetSlider, &ytBtn, &grabBtn, &clipView,
             &playBtn, &stopBtn, &revealBtn, &folderBtn, &gainKnob, &attackKnob, &releaseKnob, &gainLabel,
             &attackLabel, &releaseLabel, &statusLabel, &cancelBtn, &updateBtn })
        addAndMakeVisible (c);

    processor.engine.addChangeListener (this);
    syncFromEngine();
    startTimerHz (10);

    setResizable (true, true);
    setResizeLimits (720, 560, 1600, 1200);
    setSize (900, 680);
}

YoinkEditor::~YoinkEditor()
{
    processor.engine.removeChangeListener (this);
    list.setModel (nullptr);
    setLookAndFeel (nullptr);
}

void YoinkEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::bg);
    g.setColour (ui::line);
    g.drawHorizontalLine (52, 16.0f, (float) getWidth() - 16.0f);
    g.drawHorizontalLine (getHeight() - 36, 0.0f, (float) getWidth());
}

void YoinkEditor::resized()
{
    auto r = getLocalBounds().reduced (16, 0);

    auto top = r.removeFromTop (52);
    title.setBounds (top.removeFromLeft (140));
    stats.setBounds (top);

    auto statusBar = getLocalBounds().removeFromBottom (36).reduced (16, 6);
    updateBtn.setBounds (statusBar.removeFromRight (140));
    statusBar.removeFromRight (6);
    cancelBtn.setBounds (statusBar.removeFromRight (90));
    statusBar.removeFromRight (8);
    statusLabel.setBounds (statusBar);
    r.removeFromBottom (36 + 12);

    r.removeFromTop (10);
    auto row = r.removeFromTop (34);
    sourceLabel.setBounds (row.removeFromLeft (80));
    indexBtn.setBounds (row.removeFromRight (100));
    row.removeFromRight (6);
    limitBox.setBounds (row.removeFromRight (110));
    row.removeFromRight (6);
    sourceEdit.setBounds (row);

    r.removeFromTop (8);
    row = r.removeFromTop (34);
    queryLabel.setBounds (row.removeFromLeft (80));
    searchBtn.setBounds (row.removeFromRight (100));
    row.removeFromRight (6);
    candBox.setBounds (row.removeFromRight (110));
    row.removeFromRight (6);
    youtubeToggle.setBounds (row.removeFromRight (124));
    row.removeFromRight (6);
    queryEdit.setBounds (row);

    // abajo: clip + controles de sonido
    auto bottom = r.removeFromBottom (150);
    r.removeFromBottom (10);
    auto knobs = bottom.removeFromRight (240);
    bottom.removeFromRight (10);
    auto buttons = bottom.removeFromBottom (30);
    bottom.removeFromBottom (6);
    clipView.setBounds (bottom);
    playBtn.setBounds (buttons.removeFromLeft (90));
    buttons.removeFromLeft (6);
    stopBtn.setBounds (buttons.removeFromLeft (70));
    buttons.removeFromLeft (6);
    revealBtn.setBounds (buttons.removeFromLeft (150));
    buttons.removeFromLeft (6);
    folderBtn.setBounds (buttons.removeFromLeft (130));

    auto knobW = knobs.getWidth() / 3;
    const std::pair<juce::Slider*, juce::Label*> knobPairs[] { { &gainKnob, &gainLabel }, { &attackKnob, &attackLabel },
                                                               { &releaseKnob, &releaseLabel } };
    for (auto [knob, label] : knobPairs)
    {
        auto k = knobs.removeFromLeft (knobW);
        label->setBounds (k.removeFromTop (18));
        knob->setBounds (k.reduced (2));
    }

    // fila de recorte
    auto grabRow = r.removeFromBottom (32);
    r.removeFromBottom (8);
    grabBtn.setBounds (grabRow.removeFromRight (110));
    grabRow.removeFromRight (6);
    ytBtn.setBounds (grabRow.removeFromRight (130));
    grabRow.removeFromRight (10);
    auto half = grabRow.getWidth() / 2;
    auto a = grabRow.removeFromLeft (half);
    lengthLabel.setBounds (a.removeFromLeft (90));
    lengthSlider.setBounds (a.reduced (4, 0));
    offsetLabel.setBounds (grabRow.removeFromLeft (84));
    offsetSlider.setBounds (grabRow.reduced (4, 0));

    r.removeFromTop (12);
    hint.setBounds (r.removeFromBottom (18));
    r.removeFromBottom (4);
    list.setBounds (r);
}

void YoinkEditor::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) hits.size()))
        return;
    auto& h = hits[(size_t) row];

    g.fillAll (selected ? ui::accent.withAlpha (0.16f) : (row % 2 ? ui::panel : ui::panel.brighter (0.03f)));
    if (selected)
    {
        g.setColour (ui::accent);
        g.fillRect (0, 0, 3, height);
    }

    auto r = juce::Rectangle<int> (0, 0, width, height).reduced (12, 6);
    auto left = r.removeFromLeft (64);
    g.setColour (ui::accent);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (h.timestamp(), left.removeFromTop (20), juce::Justification::centredLeft);
    g.setColour (h.kind == "frase" ? ui::accent.withAlpha (0.8f) : ui::dim);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText (h.kind, left, juce::Justification::topLeft);

    g.setColour (ui::text);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (h.text, r.removeFromTop (20), juce::Justification::centredLeft, true);
    g.setColour (ui::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (h.title + (h.channel.isNotEmpty() ? yoink::T8 ("  ·  ") + h.channel : juce::String()), r,
                juce::Justification::centredLeft, true);
}

void YoinkEditor::doIndex()
{
    processor.lastSource = sourceEdit.getText().trim();
    processor.engine.indexSource (processor.lastSource, limitBox.getSelectedId() > 0 ? limitBox.getSelectedId() : 10);
}

void YoinkEditor::doSearch()
{
    processor.lastQuery = queryEdit.getText().trim();
    if (processor.searchAllYouTube)
        processor.engine.searchYouTube (processor.lastQuery, processor.youtubeCandidates);
    else
        processor.engine.search (processor.lastQuery);
}

void YoinkEditor::grab (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) hits.size()))
        return;
    auto& h = hits[(size_t) row];
    GrabRequest req;
    req.videoId = h.videoId;
    req.start = juce::jmax (0.0, h.start + offsetSlider.getValue());
    req.duration = lengthSlider.getValue();
    req.label = query.isNotEmpty() ? query : h.title;
    processor.engine.grab (req);
}

void YoinkEditor::changeListenerCallback (juce::ChangeBroadcaster*) { syncFromEngine(); }

void YoinkEditor::syncFromEngine()
{
    auto s = processor.engine.snapshot();

    statusLabel.setText ((s.busy ? juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f ")) : juce::String()) + s.status,
                         juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, s.isError ? ui::error : (s.busy ? ui::accent : ui::text));
    cancelBtn.setEnabled (s.busy);
    indexBtn.setEnabled (! s.busy);
    searchBtn.setEnabled (! s.busy);
    grabBtn.setEnabled (! s.busy);
    updateBtn.setEnabled (! s.busy);

    stats.setText (juce::String (s.stats.videos) + yoink::T8 (" videos  ·  ") + juce::String (s.stats.lines) + yoink::T8 (" líneas  ·  ")
                       + juce::String (s.stats.clips) + yoink::T8 (" clips  ·  ") + juce::String (s.stats.sources) + " fuentes",
                   juce::dontSendNotification);

    if (s.lastQuery != query || s.hits.size() != hits.size()
        || ! std::equal (s.hits.begin(), s.hits.end(), hits.begin(), hits.end(),
                         [] (const Hit& a, const Hit& b) { return a.videoId == b.videoId && a.start == b.start; }))
    {
        hits = s.hits;
        query = s.lastQuery;
        list.updateContent();
        list.repaint();
        if (! hits.empty())
            list.selectRow (0);
    }

    if (processor.getClip() != shownClip)
    {
        shownClip = processor.getClip();
        clipView.setFile (shownClip);
    }
}

void YoinkEditor::timerCallback()
{
    auto playing = processor.isPreviewing();
    playBtn.setColour (juce::TextButton::buttonColourId, playing ? ui::accent.withAlpha (0.5f) : ui::panel);
    auto hasClip = processor.getClip().existsAsFile();
    playBtn.setEnabled (hasClip);
    stopBtn.setEnabled (hasClip);
    revealBtn.setEnabled (hasClip);
    auto hasRow = juce::isPositiveAndBelow (list.getSelectedRow(), (int) hits.size());
    ytBtn.setEnabled (hasRow);
    grabBtn.setEnabled (hasRow && ! processor.engine.snapshot().busy);
}
