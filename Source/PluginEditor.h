#pragma once

#include "PluginProcessor.h"

class AutoMasterEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AutoMasterEditor (AutoMasterProcessor&);
    ~AutoMasterEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Row
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };

    AutoMasterProcessor& proc;
    std::array<Row, 7> rows;
    juce::TextButton learnBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> learnAtt;
    juce::Label statusLbl, infoLbl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoMasterEditor)
};
