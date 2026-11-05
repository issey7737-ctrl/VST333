#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout AutoMasterProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "learn", 1 }, "Apprentissage", false));
    l.add (std::make_unique<F> (juce::ParameterID { "eqAmount", 1 }, "Intensite EQ auto", R (0.f, 100.f, 1.f), 70.f));
    l.add (std::make_unique<F> (juce::ParameterID { "tilt", 1 }, "Tonalite", R (-3.f, 3.f, 0.1f), 0.f));
    l.add (std::make_unique<F> (juce::ParameterID { "comp", 1 }, "Compression douce", R (0.f, 100.f, 1.f), 40.f));
    l.add (std::make_unique<F> (juce::ParameterID { "width", 1 }, "Largeur stereo", R (50.f, 150.f, 1.f), 100.f));
    l.add (std::make_unique<F> (juce::ParameterID { "target", 1 }, "Volume cible", R (-20.f, -6.f, 0.1f), -14.f));
    l.add (std::make_unique<F> (juce::ParameterID { "ceiling", 1 }, "Plafond limiteur", R (-3.f, -0.1f, 0.1f), -1.f));
    l.add (std::make_unique<F> (juce::ParameterID { "trim", 1 }, "Gain de sortie", R (-6.f, 6.f, 0.1f), 0.f));
    return l;
}

AutoMasterProcessor::AutoMasterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pEq = apvts.getRawParameterValue ("eqAmount");
    pTilt = apvts.getRawParameterValue ("tilt");
    pComp = apvts.getRawParameterValue ("comp");
    pWidth = apvts.getRawParameterValue ("width");
    pTarget = apvts.getRawParameterValue ("target");
    pCeil = apvts.getRawParameterValue ("ceiling");
    pTrim = apvts.getRawParameterValue ("trim");
    pLearn = apvts.getRawParameterValue ("learn");
}

bool AutoMasterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void AutoMasterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (engine.getLatency());
}

void AutoMasterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2) return;

    am::Engine::Params p;
    p.eqAmount = pEq->load() / 100.f;
    p.tilt = pTilt->load();
    p.comp = pComp->load() / 100.f;
    p.width = pWidth->load() / 100.f;
    p.targetLufs = pTarget->load();
    p.ceilingDb = pCeil->load();
    p.trimDb = pTrim->load();
    engine.setParams (p);

    const bool wantLearn = pLearn->load() > 0.5f;
    if (wantLearn && ! engine.isLearning()) engine.startLearn();
    else if (! wantLearn && engine.isLearning()) engine.stopLearn();

    engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples());
}

juce::AudioProcessorEditor* AutoMasterProcessor::createEditor() { return new AutoMasterEditor (*this); }

void AutoMasterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("learned", engine.getLearnedString(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AutoMasterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto vt = juce::ValueTree::fromXml (*xml);
        if (vt.isValid())
        {
            const auto learned = vt.getProperty ("learned").toString();
            apvts.replaceState (vt);
            // on ne reprend pas l'etat "apprentissage en cours"
            if (auto* lp = apvts.getParameter ("learn")) lp->setValueNotifyingHost (0.f);
            if (learned.isNotEmpty()) engine.setLearnedString (learned);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AutoMasterProcessor(); }
