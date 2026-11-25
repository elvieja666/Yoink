#pragma once
#include "PluginProcessor.h"

namespace yoink::ui
{
const juce::Colour bg      { 0xff121316 };
const juce::Colour panel   { 0xff1b1d22 };
const juce::Colour line    { 0xff2a2d34 };
const juce::Colour text    { 0xffe8e6e1 };
const juce::Colour dim     { 0xff8b8f98 };
const juce::Colour accent  { 0xffffc23d };
const juce::Colour error   { 0xffff6b5e };
}

/** Onda del clip: se arrastra al DAW como archivo. */
class ClipView : public juce::Component, private juce::ChangeListener
{
public:
    explicit ClipView (YoinkProcessor&);
    ~ClipView() override;

    void setFile (const juce::File&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    YoinkProcessor& processor;
    juce::AudioThumbnailCache cache { 4 };
    juce::AudioThumbnail thumbnail;
    juce::File file;
    bool dragging = false;
};

class YoinkEditor : public juce::AudioProcessorEditor,
                    private juce::ChangeListener,
                    private juce::ListBoxModel,
                    private juce::Timer
{
public:
    explicit YoinkEditor (YoinkProcessor&);
    ~YoinkEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // ListBoxModel
    int getNumRows() override { return (int) hits.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { grab (row); }
    void returnKeyPressed (int row) override { grab (row); }

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void doIndex();
    void doSearch();
    void grab (int row);
    void syncFromEngine();

    YoinkProcessor& processor;
    juce::LookAndFeel_V4 lnf;

    juce::Label title, stats, sourceLabel, queryLabel, lengthLabel, offsetLabel, statusLabel, hint;
    juce::TextEditor sourceEdit, queryEdit;
    juce::ComboBox limitBox, candBox;
    juce::ToggleButton youtubeToggle;
    juce::TextButton indexBtn { "Indexar" }, searchBtn { "Buscar" }, grabBtn { "Yoink!" }, ytBtn { "Ver en YouTube" },
                     playBtn { "Escuchar" }, stopBtn { "Parar" }, revealBtn { "Mostrar en Finder" },
                     cancelBtn { "Cancelar" }, updateBtn { "Actualizar yt-dlp" }, folderBtn { "Carpeta de clips" };
    juce::ListBox list;
    juce::Slider lengthSlider, offsetSlider, gainKnob, attackKnob, releaseKnob;
    juce::Label gainLabel, attackLabel, releaseLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAtt, attackAtt, releaseAtt;
    ClipView clipView;

    std::vector<yoink::Hit> hits;
    juce::String query;
    juce::File shownClip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YoinkEditor)
};
