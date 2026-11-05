#include "PluginEditor.h"

static juce::String T (const char* s) { return juce::String::fromUTF8 (s); }

AutoMasterEditor::AutoMasterEditor (AutoMasterProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    struct Def { const char* id; const char* name; const char* suffix; };
    static const Def defs[] = {
        { "eqAmount", "Intensité de l'EQ automatique", " %" },
        { "tilt",     "Tonalité (sombre ↔ clair)", " dB" },
        { "comp",     "Compression douce (liant)", " %" },
        { "width",    "Largeur stéréo", " %" },
        { "target",   "Volume visé", " LUFS" },
        { "ceiling",  "Plafond du limiteur", " dB" },
        { "trim",     "Ajustement du gain de sortie", " dB" },
    };

    for (size_t i = 0; i < rows.size(); ++i)
    {
        auto& r = rows[i];
        r.label.setText (T (defs[i].name), juce::dontSendNotification);
        r.label.setColour (juce::Label::textColourId, juce::Colour (0xffe8e0d4));
        addAndMakeVisible (r.label);
        r.slider.setSliderStyle (juce::Slider::LinearHorizontal);
        r.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 90, 22);
        r.slider.setTextValueSuffix (T (defs[i].suffix));
        addAndMakeVisible (r.slider);
        r.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, defs[i].id, r.slider);
    }

    learnBtn.setButtonText (T ("1. Apprendre le morceau (activer pendant la lecture)"));
    learnBtn.setClickingTogglesState (true);
    learnBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a3128));
    learnBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffc8501e));
    addAndMakeVisible (learnBtn);
    learnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "learn", learnBtn);

    for (auto* l : { &statusLbl, &infoLbl })
    {
        l->setColour (juce::Label::textColourId, juce::Colour (0xfff1e9dc));
        l->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*l);
    }
    statusLbl.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    infoLbl.setFont (juce::FontOptions (13.0f));

    setSize (560, 520);
    startTimerHz (8);
    timerCallback();
}

void AutoMasterEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff211b15));
    g.setColour (juce::Colour (0xffc8501e));
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("AutoMaster", 18, 12, 300, 34, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xffa89a88));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (T ("Mastering adaptatif : écoute le morceau, règle l'EQ, le volume et le limiteur"), 18, 44, 520, 20, juce::Justification::centredLeft);
}

void AutoMasterEditor::resized()
{
    auto area = getLocalBounds().reduced (18);
    area.removeFromTop (54);
    learnBtn.setBounds (area.removeFromTop (46));
    area.removeFromTop (8);
    statusLbl.setBounds (area.removeFromTop (26));
    infoLbl.setBounds (area.removeFromTop (22));
    area.removeFromTop (10);
    for (auto& r : rows)
    {
        auto row = area.removeFromTop (46);
        r.label.setBounds (row.removeFromTop (20));
        r.slider.setBounds (row);
    }
}

void AutoMasterEditor::timerCallback()
{
    auto& e = proc.engine;
    juce::String s;
    switch (e.status.load())
    {
        case am::Engine::Learning:  s = T ("Écoute en cours… joue la partie la plus forte du morceau (refrain)"); break;
        case am::Engine::Ok:        s = juce::String::formatted (T ("Morceau appris : %.1f LUFS (%.0f s analysées)").toRawUTF8(), (double) e.learnedLufs.load(), (double) e.learnedSeconds.load()); break;
        case am::Engine::TooLittle: s = T ("Pas assez de son pour analyser : rejoue 20 à 30 secondes"); break;
        default:                    s = T ("Aucun apprentissage : active le bouton ci-dessus et lance la lecture"); break;
    }
    statusLbl.setText (s, juce::dontSendNotification);

    infoLbl.setText (juce::String::formatted (T ("Sortie : %.1f LUFS    |    Limiteur : %.1f dB    |    Gain auto : %+.1f dB").toRawUTF8(),
                                              (double) e.outLufs.load(), (double) e.grDb.load(), (double) e.autoTrimShown.load()),
                     juce::dontSendNotification);
}
