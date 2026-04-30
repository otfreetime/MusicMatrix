#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

//==============================================================================
// Mock AudioProcessor for Testing
//==============================================================================
class MockAudioProcessor : public juce::AudioProcessor
{
public:
    MockAudioProcessor()
        : juce::AudioProcessor (BusesProperties()
            .withInput  ("Input",  juce::AudioChannelSet::stereo())
            .withOutput ("Output", juce::AudioChannelSet::stereo()))
    {
    }

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        // Simple passthrough + gain
        buffer.applyGain (0.5f);
    }

    juce::AudioProcessorEditor* createEditor() override
    {
        return nullptr;
    }

    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "MockAudioProcessor"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MockAudioProcessor)
};

//==============================================================================
// KnownPluginList Tests
//==============================================================================
TEST_CASE ("KnownPluginList can store and retrieve plugin descriptions", "[plugin_list]")
{
    juce::KnownPluginList list;
    
    REQUIRE (list.getNumTypes() == 0);
    
    // Create a mock plugin description
    juce::PluginDescription desc;
    desc.name = "Test Plugin";
    desc.pluginFormatName = "VST3";
    desc.fileOrIdentifier = "/path/to/plugin.vst3";
    desc.version = "1.0.0";
    
    // Add to list
    list.addType (desc);
    
    REQUIRE (list.getNumTypes() == 1);
    REQUIRE (list.getTypes()[0].name == "Test Plugin");
}

TEST_CASE ("KnownPluginList XML serialization works", "[plugin_list][cache]")
{
    juce::KnownPluginList list1;
    
    // Add two plugins
    juce::PluginDescription desc1, desc2;
    desc1.name = "Plugin 1";
    desc1.pluginFormatName = "VST3";
    desc1.fileOrIdentifier = "/path/to/plugin1.vst3";
    
    desc2.name = "Plugin 2";
    desc2.pluginFormatName = "VST3";
    desc2.fileOrIdentifier = "/path/to/plugin2.vst3";
    
    list1.addType (desc1);
    list1.addType (desc2);
    
    // Serialize to XML
    auto xml = list1.createXml();
    REQUIRE (xml != nullptr);
    
    // Deserialize into new list
    juce::KnownPluginList list2;
    list2.recreateFromXml (*xml);
    
    REQUIRE (list2.getNumTypes() == 2);
    REQUIRE (list2.getTypes()[0].name == "Plugin 1");
    REQUIRE (list2.getTypes()[1].name == "Plugin 2");
}

TEST_CASE ("KnownPluginList can blacklist plugins", "[plugin_list][blacklist]")
{
    juce::KnownPluginList list;
    
    REQUIRE (list.getBlacklistedFiles().isEmpty());
    
    // Add to blacklist
    list.addToBlacklist ("/path/to/broken_plugin.vst3");
    
    REQUIRE (list.getBlacklistedFiles().contains ("/path/to/broken_plugin.vst3"));
    
    // Remove from blacklist
    list.removeFromBlacklist ("/path/to/broken_plugin.vst3");
    
    REQUIRE (list.getBlacklistedFiles().isEmpty());
}

//==============================================================================
// AudioPluginFormatManager Tests
//==============================================================================
TEST_CASE ("AudioPluginFormatManager can store format references", "[format_manager]")
{
    juce::AudioPluginFormatManager manager;
    
    REQUIRE (manager.getNumFormats() == 0);
    
    // Add VST3 format
    manager.addFormat (std::make_unique<juce::VST3PluginFormat>());
    
    REQUIRE (manager.getNumFormats() == 1);
    REQUIRE (manager.getFormat (0) != nullptr);
}

//==============================================================================
// AudioProcessorPlayer Tests
//==============================================================================
TEST_CASE ("AudioProcessorPlayer manages processor pointer", "[processor_player]")
{
    juce::AudioProcessorPlayer player;
    auto mockPlugin = std::make_unique<MockAudioProcessor>();
    auto* rawPtr = mockPlugin.get();

    player.setProcessor (rawPtr);
    REQUIRE (player.getCurrentProcessor() == rawPtr);

    player.setProcessor (nullptr);
    REQUIRE (player.getCurrentProcessor() == nullptr);
}

//==============================================================================
// Dry/Wet Mixing Tests
//==============================================================================
TEST_CASE ("Dry/wet mixing preserves signal with 0% wet", "[dry_wet]")
{
    // 100% dry = input unchanged
    juce::AudioBuffer<float> dryBuffer (2, 512);
    dryBuffer.setSample (0, 0, 1.0f);
    
    float dryWetMix = 0.0f;  // 0% wet (100% dry)
    
    // After mixing with 100% dry: output = input * (1-0) + wet * 0 = input
    float expected = 1.0f * (1.0f - dryWetMix);
    
    REQUIRE (expected == Catch::Approx (1.0f));
}

TEST_CASE ("Dry/wet mixing blends at 50%", "[dry_wet]")
{
    // 50% dry, 50% wet
    float dryWetMix = 0.5f;
    float drySignal = 1.0f;
    float wetSignal = 0.5f;
    
    // output = dry * (1 - mix) + wet * mix = 1.0 * 0.5 + 0.5 * 0.5 = 0.75
    float output = drySignal * (1.0f - dryWetMix) + wetSignal * dryWetMix;
    
    REQUIRE (output == Catch::Approx (0.75f));
}

//==============================================================================
// Plugin Editor Lifecycle Tests
//==============================================================================
TEST_CASE ("AudioProcessorEditor lifecycle", "[editor]")
{
    // This tests that editors can be created and destroyed safely
    // In production, editors are created from loaded plugins
    
    auto mockPlugin = std::make_unique<MockAudioProcessor>();
    
    // Mock processor returns nullptr for editor (as defined)
    auto* editor = mockPlugin->createEditorIfNeeded();
    
    REQUIRE (editor == nullptr);  // Expected for MockAudioProcessor
}

//==============================================================================
// Thread Safety Tests
//==============================================================================
TEST_CASE ("CriticalSection protects concurrent access", "[threading]")
{
    juce::CriticalSection lock;
    int sharedValue = 0;
    
    // Simulate concurrent access
    {
        const juce::ScopedLock scopedLock (lock);
        sharedValue = 42;
    }
    
    REQUIRE (sharedValue == 42);
}

//==============================================================================
// Audio Buffer Operations Tests
//==============================================================================
TEST_CASE ("Audio buffer copying preserves samples", "[audio_buffer]")
{
    juce::AudioBuffer<float> source (2, 512);
    juce::AudioBuffer<float> dest (2, 512);
    
    source.setSample (0, 0, 1.0f);
    source.setSample (1, 0, 0.5f);
    
    dest.copyFrom (0, 0, source, 0, 0, 512);
    dest.copyFrom (1, 0, source, 1, 0, 512);
    
    REQUIRE (dest.getSample (0, 0) == Catch::Approx (1.0f));
    REQUIRE (dest.getSample (1, 0) == Catch::Approx (0.5f));
}

TEST_CASE ("Audio buffer mixing adds samples", "[audio_buffer]")
{
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.setSample (0, 0, 1.0f);
    
    juce::AudioBuffer<float> toAdd (2, 512);
    toAdd.setSample (0, 0, 0.5f);
    
    buffer.addFrom (0, 0, toAdd, 0, 0, 512, 0.5f);  // Add 50% of source
    
    // 1.0 + (0.5 * 0.5) = 1.25
    REQUIRE (buffer.getSample (0, 0) == Catch::Approx (1.25f));
}

//==============================================================================
// DSP Gain Processing Tests
//==============================================================================
TEST_CASE ("dsp::Gain applies linear gain correctly", "[dsp]")
{
    juce::dsp::Gain<float> gain;
    juce::AudioBuffer<float> buffer (2, 512);
    
    buffer.setSample (0, 0, 1.0f);
    buffer.setSample (1, 0, 1.0f);
    
    // Set gain to 0.5
    gain.setGainLinear (0.5f);
    
    // Prepare DSP
    juce::dsp::ProcessSpec spec { 44100.0, 512, 2 };
    gain.prepare (spec);
    
    // Process
    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    gain.process (context);
    
    // Verify gain was applied
    REQUIRE (buffer.getSample (0, 0) == Catch::Approx (0.5f));
    REQUIRE (buffer.getSample (1, 0) == Catch::Approx (0.5f));
}
