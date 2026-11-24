#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Core/Engine.h"

/** Instrumento: el último clip recortado se toca por MIDI (Do3 = altura original). */
class YoinkProcessor : public juce::AudioProcessor
{
public:
    YoinkProcessor();
    ~YoinkProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    /** Carga un WAV en el sampler (hilo de mensajes). */
    bool loadClip (const juce::File& file);
    juce::File getClip() const { return clipFile; }
    double getClipSeconds() const { return clipSeconds; }

    void previewStart() { previewRequest = 1; }
    void previewStop()  { previewRequest = 2; }
    bool isPreviewing() const { return previewing.load(); }

    yoink::Engine engine;
    juce::AudioProcessorValueTreeState params;
    juce::AudioFormatManager formats;

    // ajustes de la UI que se guardan con el proyecto
    double clipLength = 4.0;
    double clipOffset = -0.5;
    juce::String lastQuery, lastSource;

    static constexpr int rootNote = 60;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::Synthesiser synth;
    juce::File clipFile;
    double clipSeconds = 0;
    std::atomic<int> previewRequest { 0 };
    std::atomic<bool> previewing { false };
    juce::LinearSmoothedValue<float> gainSmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YoinkProcessor)
};
