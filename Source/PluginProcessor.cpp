#include "PluginProcessor.h"

// create parameter layout
static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
	std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    juce::NormalisableRange range(0.f, 127.f, 1.f);
    const auto cat = juce::AudioProcessorParameter::genericParameter;

    const auto valToStr = [](float value, int)
    {
		auto val = static_cast<int>(std::round(value));
        // show as pitch
		return juce::MidiMessage::getMidiNoteName(val, true, true, 3);
    };
    const auto strToVal = [](const juce::String& str)
    {
		// parse pitch
		const auto text = str.toLowerCase();
		bool hasLetters = false;
		for (auto i = 0; i < text.length(); ++i)
		{
			auto chr = text[i];
			bool isDigit = chr >= '0' && chr <= '9';
			if (!isDigit)
				hasLetters = true;
		}
		if(!hasLetters)
			return static_cast<float>(str.getIntValue());

		enum pitchclass { C, Db, D, Eb, E, F, Gb, G, Ab, A, Bb, B, Num };
		enum class State { Pitchclass, FlatOrSharp, Parse, numStates };

		auto state = State::Pitchclass;

		int val = -1;

		for (auto i = 0; i < text.length(); ++i)
		{
			auto chr = text[i];

			if (state == State::Pitchclass)
			{
				if (chr == 'c')
					val = C;
				else if (chr == 'd')
					val = D;
				else if (chr == 'e')
					val = E;
				else if (chr == 'f')
					val = F;
				else if (chr == 'g')
					val = G;
				else if (chr == 'a')
					val = A;
				else if (chr == 'b')
					val = B;
				else
					return 69.f;

				state = State::FlatOrSharp;
			}
			else if (state == State::FlatOrSharp)
			{
				if (chr == '#')
					++val;
				else if (chr == 'b')
					--val;
				else
					--i;

				state = State::Parse;
			}
			else if (state == State::Parse)
			{
				auto newVal = static_cast<int>(text.substring(i).getFloatValue());
				if (newVal == -1)
					return 69.f;
				val += 24 + newVal * 12;
				while (val < 0)
					val += 12;
				return static_cast<float>(val);
			}
			else
				return 69.f;
		}

		return juce::jlimit(0.f, 127.f, static_cast<float>(val + 24));
    };

	params.push_back(std::make_unique<juce::AudioParameterFloat>
    (
        "lowerLimit", "Lower Limit", range, 0.f, "Lower Limit", cat, valToStr, strToVal
    ));
	params.push_back(std::make_unique<juce::AudioParameterFloat>
    (
        "upperLimit", "Upper Limit", range, 127.f, "Upper Limit", cat, valToStr, strToVal
    ));
	return { params.begin(), params.end() };
}


MIDIWrapAudioProcessor::MIDIWrapAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
	apvts(*this, nullptr, "PARAMETERS", createParameterLayout()),
	lowerLimitParam(apvts.getParameter("lowerLimit")),
	upperLimitParam(apvts.getParameter("upperLimit")),
    midiBufferOut(),
	lowerLimit(0),
	upperLimit(127)
#endif
{
    midiBufferOut.ensureSize(1024);
}

const juce::String MIDIWrapAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool MIDIWrapAudioProcessor::acceptsMidi() const
{
    return true;
}

bool MIDIWrapAudioProcessor::producesMidi() const
{
    return true;
}

bool MIDIWrapAudioProcessor::isMidiEffect() const
{
    return true;
}

double MIDIWrapAudioProcessor::getTailLengthSeconds() const
{
    return 0.;
}

int MIDIWrapAudioProcessor::getNumPrograms()
{
    return 1;
}

int MIDIWrapAudioProcessor::getCurrentProgram()
{
    return 0;
}

void MIDIWrapAudioProcessor::setCurrentProgram (int)
{
}

const juce::String MIDIWrapAudioProcessor::getProgramName(int)
{
    return {};
}

void MIDIWrapAudioProcessor::changeProgramName (int, const juce::String&)
{
}

void MIDIWrapAudioProcessor::prepareToPlay (double, int)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
}

void MIDIWrapAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool MIDIWrapAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

inline int wrap(int x, int lower, int upper) noexcept
{
    while (x < lower)
        x += 12;
	while (x > upper)
		x -= 12;
	return x;
}

void MIDIWrapAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const auto totalNumInputChannels  = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();
    const auto numSamples = buffer.getNumSamples();
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, numSamples);
    if (numSamples == 0)
        return;

    auto& range = lowerLimitParam->getNormalisableRange();

    const auto nLowerLimit = static_cast<int>(std::round(range.convertFrom0to1(lowerLimitParam->getValue())));
    const auto nUpperLimit = static_cast<int>(std::round(range.convertFrom0to1(upperLimitParam->getValue())));
	
	midiBufferOut.clear();

	if (lowerLimit != nLowerLimit || upperLimit != nUpperLimit)
	{
		lowerLimit = nLowerLimit;
		upperLimit = nUpperLimit;
		
		// all notes off
		for (auto ch = 1; ch <= 16; ++ch)
			for (auto note = 0; note < 128; ++note)
				midiBufferOut.addEvent(juce::MidiMessage::noteOff(ch, note), 0);
	}
	
	for (auto midiMessage : midiMessages)
	{
		auto msg = midiMessage.getMessage();
		const auto sampleNumber = midiMessage.samplePosition;

		if (msg.isNoteOn())
		{
			const auto noteNumber = wrap(midiMessage.getMessage().getNoteNumber(), lowerLimit, upperLimit);
			msg = juce::MidiMessage::noteOn(msg.getChannel(), noteNumber, msg.getVelocity());

			midiBufferOut.addEvent(msg, sampleNumber);
		}
		else if (msg.isNoteOff())
		{
			const auto noteNumber = wrap(midiMessage.getMessage().getNoteNumber(), lowerLimit, upperLimit);
			msg = juce::MidiMessage::noteOff(msg.getChannel(), noteNumber, msg.getVelocity());

			midiBufferOut.addEvent(msg, sampleNumber);
		}
		else
		{
			midiBufferOut.addEvent(msg, sampleNumber);
		}
	}

	midiMessages.swapWith(midiBufferOut);
}

//==============================================================================
bool MIDIWrapAudioProcessor::hasEditor() const
{
    return false;
}

juce::AudioProcessorEditor* MIDIWrapAudioProcessor::createEditor()
{
    return nullptr;
}

//==============================================================================
void MIDIWrapAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void MIDIWrapAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MIDIWrapAudioProcessor();
}
