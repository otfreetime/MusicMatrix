#include "MainComponent.h"
#include "bridge/AudioSharedMemory.h"

//==============================================================================
// Audio Processing Implementation
//==============================================================================

void MainComponent::openAudioFile()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Select an audio file to play...",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav;*.mp3;*.aiff;*.flac;*.ogg");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                        | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser] (const juce::FileChooser& fc)
        {
            if (fc.getResults().isEmpty())
                return;

            auto file = fc.getResult();
            auto* reader = audioFormatManager.createReaderFor (file);

            if (reader != nullptr)
            {
                auto newSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
                transportSource.setSource (newSource.get(), 0, nullptr,
                                           reader->sampleRate);
                readerSource = std::move (newSource);

                uiController.getPlayButton()->setEnabled (true);
                uiController.getStopButton()->setEnabled (false);
                uiController.setStatusMessage ("Loaded: " + file.getFileName());
            }
            else
            {
                uiController.setStatusMessage ("Could not read file: " + file.getFileName());
            }
        });
}

void MainComponent::playAudioFile()
{
    transportSource.setPosition (0.0);
    transportSource.start();
    uiController.getPlayButton()->setEnabled (false);
    uiController.getStopButton()->setEnabled (true);
    uiController.setStatusMessage ("Playing audio...");
}

void MainComponent::stopAudioFile()
{
    transportSource.stop();
    uiController.getPlayButton()->setEnabled (true);
    uiController.getStopButton()->setEnabled (false);
    uiController.setStatusMessage ("Stopped.");
}
void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    currentSampleRate = sampleRate;
    blockSize = samplesPerBlockExpected;

    audioFormatManager.registerBasicFormats();
    transportSource.prepareToPlay (samplesPerBlockExpected, sampleRate);

    dryGain.setGainLinear (dryGainLinear);

    if (auto* plugin = pluginManager.getLoadedPlugin())
    {
        plugin->prepareToPlay (sampleRate, samplesPerBlockExpected);
    }
    
    if (bridgeManager.isAvailable())
    {
        juce::String payload = juce::String(sampleRate) + "," + juce::String(samplesPerBlockExpected);
        bridgeManager.sendCommand({ myapp::bridge::IPCCommandType::setupAudio, payload });
    }
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    bufferToFill.clearActiveBufferRegion();

    const auto numSamples = bufferToFill.numSamples;

    // If audio file is playing, fill from transport source and return
    if (transportSource.isPlaying())
    {
        transportSource.getNextAudioBlock (bufferToFill);
        return;
    }

    // Process bridged VST2 plugin if loaded
    if (bridgePluginLoaded && bridgeManager.isAudioOpen())
    {
        // Pull audio from the ring buffer the worker continuously fills.
        // audioRingBufferRead handles underruns gracefully (silence fill).
        if (auto* outMem = bridgeManager.getAudioOutputMemory().get())
        {
            float* chPtrs[2] = {
                bufferToFill.buffer->getWritePointer (0, bufferToFill.startSample),
                bufferToFill.buffer->getNumChannels() > 1
                    ? bufferToFill.buffer->getWritePointer (1, bufferToFill.startSample)
                    : bufferToFill.buffer->getWritePointer (0, bufferToFill.startSample)
            };
            myapp::bridge::audioRingBufferRead (outMem->getData(), chPtrs, numSamples);
        }
    }
    // Process local VST3 plugin if loaded
    else if (auto* plugin = pluginManager.getLoadedPlugin())
    {
        juce::AudioBuffer<float> tempBuffer (2, numSamples);
        tempBuffer.clear();
        
        juce::MidiBuffer emptyMidi;
        plugin->processBlock (tempBuffer, emptyMidi);
        
        for (int ch = 0; ch < juce::jmin(2, bufferToFill.buffer->getNumChannels()); ++ch)
        {
            bufferToFill.buffer->copyFrom (ch, bufferToFill.startSample, tempBuffer.getReadPointer (ch), numSamples);
        }
    }
}

void MainComponent::releaseResources()
{
    transportSource.releaseResources();

    if (auto* plugin = pluginManager.getLoadedPlugin())
    {
        plugin->releaseResources();
    }
}

void MainComponent::processDryWet (juce::AudioBuffer<float>& buffer, int numSamples)
{
    dryGainLinear = dryWetMix;
    dryGain.setGainLinear (dryGainLinear);

    juce::AudioBuffer<float> dryBuffer (2, numSamples);
    dryBuffer.makeCopyOf (buffer);

    if (auto* plugin = pluginManager.getLoadedPlugin())
    {
        juce::AudioBuffer<float> tempBuffer (2, numSamples);
        tempBuffer.clear();
        
        juce::MidiBuffer emptyMidi;
        plugin->processBlock (tempBuffer, emptyMidi);
        
        for (int ch = 0; ch < 2; ++ch)
        {
            buffer.copyFrom (ch, 0, tempBuffer.getReadPointer (ch), numSamples);
        }
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* wetData = buffer.getWritePointer (ch);
        const auto* dryData = dryBuffer.getReadPointer (ch);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            wetData[sample] = dryData[sample] * (1.0f - dryWetMix) + wetData[sample] * dryWetMix;
        }
    }
}
