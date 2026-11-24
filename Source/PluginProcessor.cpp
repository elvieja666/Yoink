#include "PluginProcessor.h"
#include "PluginEditor.h"

YoinkProcessor::YoinkProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "YoinkParams", createLayout())
{
    formats.registerBasicFormats();
    for (int i = 0; i < 8; ++i)
        synth.addVoice (new juce::SamplerVoice());

    engine.onClipReady = [this] (const juce::File& f) { loadClip (f); };
    engine.refreshStats();
}

YoinkProcessor::~YoinkProcessor()
{
    engine.onClipReady = nullptr;
}

juce::AudioProcessorValueTreeState::ParameterLayout YoinkProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "gain", 1 }, "Ganancia",
                                                       NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "attack", 1 }, "Ataque",
                                                       NormalisableRange<float> (0.0f, 500.0f, 0.1f, 0.4f), 2.0f,
                                                       AudioParameterFloatAttributes().withLabel ("ms")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "release", 1 }, "Release",
                                                       NormalisableRange<float> (5.0f, 3000.0f, 0.1f, 0.4f), 60.0f,
                                                       AudioParameterFloatAttributes().withLabel ("ms")));
    return layout;
}

bool YoinkProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void YoinkProcessor::prepareToPlay (double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
    gainSmoothed.reset (sampleRate, 0.02);
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (params.getRawParameterValue ("gain")->load()));
}

void YoinkProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    switch (previewRequest.exchange (0))
    {
        case 1:
            midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, rootNote, (juce::uint8) 110), 0);
            break;
        case 2:
            midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            synth.allNotesOff (0, false);
            break;
        default: break;
    }

    if (synth.getNumSounds() > 0)
        if (auto* sound = dynamic_cast<juce::SamplerSound*> (synth.getSound (0).get()))
        {
            juce::ADSR::Parameters env;
            env.attack = params.getRawParameterValue ("attack")->load() / 1000.0f;
            env.decay = 0.0f;
            env.sustain = 1.0f;
            env.release = params.getRawParameterValue ("release")->load() / 1000.0f;
            sound->setEnvelopeParameters (env);
        }

    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());

    gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (params.getRawParameterValue ("gain")->load()));
    if (gainSmoothed.isSmoothing())
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            auto g = gainSmoothed.getNextValue();
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.getWritePointer (ch)[i] *= g;
        }
    else
        buffer.applyGain (gainSmoothed.getTargetValue());

    bool active = false;
    for (int i = 0; i < synth.getNumVoices(); ++i)
        active = active || synth.getVoice (i)->isVoiceActive();
    previewing = active;
}

bool YoinkProcessor::loadClip (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return false;

    juce::BigInteger notes;
    notes.setRange (0, 128, true);
    const double seconds = (double) reader->lengthInSamples / juce::jmax (1.0, reader->sampleRate);
    auto* sound = new juce::SamplerSound ("clip", *reader, notes, rootNote,
                                          params.getRawParameterValue ("attack")->load() / 1000.0,
                                          params.getRawParameterValue ("release")->load() / 1000.0,
                                          juce::jmin (130.0, seconds + 1.0));
    synth.allNotesOff (0, false);
    synth.clearSounds();
    synth.addSound (sound);
    clipFile = file;
    clipSeconds = seconds;
    engine.sendChangeMessage();
    return true;
}

juce::AudioProcessorEditor* YoinkProcessor::createEditor()
{
    return new YoinkEditor (*this);
}

void YoinkProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = params.copyState();
    state.setProperty ("clip", clipFile.getFullPathName(), nullptr);
    state.setProperty ("clipLength", clipLength, nullptr);
    state.setProperty ("clipOffset", clipOffset, nullptr);
    state.setProperty ("lastQuery", lastQuery, nullptr);
    state.setProperty ("lastSource", lastSource, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void YoinkProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (params.state.getType()))
        return;
    auto state = juce::ValueTree::fromXml (*xml);
    params.replaceState (state);
    clipLength = (double) state.getProperty ("clipLength", 4.0);
    clipOffset = (double) state.getProperty ("clipOffset", -0.5);
    lastQuery = state.getProperty ("lastQuery").toString();
    lastSource = state.getProperty ("lastSource").toString();

    auto path = state.getProperty ("clip").toString();
    if (path.isNotEmpty() && juce::File::isAbsolutePath (path) && juce::File (path).existsAsFile())
    {
        auto file = juce::File (path);
        juce::MessageManager::callAsync ([this, file, alive = juce::WeakReference<yoink::Engine> (&engine)]
        {
            if (alive != nullptr)
                loadClip (file);
        });
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new YoinkProcessor();
}
