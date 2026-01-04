juce_add_plugin(CompassCompressor
    COMPANY_NAME "Compass"
    PLUGIN_NAME "CompassCompressor"
    PLUGIN_MANUFACTURER_CODE CmpA
    PLUGIN_CODE CmpC
    FORMATS VST3
    PRODUCT_NAME "Compass Compressor"
    BUNDLE_ID "com.CompassAudio.CompassCompressor"
    COPY_PLUGIN_AFTER_BUILD TRUE
)

# Generate JuceHeader.h for <JuceHeader.h> includes
juce_generate_juce_header(CompassCompressor)
# Ensure VST3 wrapper does not enable VST2 replacement/compat guards
if (TARGET CompassCompressor_VST3)
  target_compile_definitions(CompassCompressor_VST3 PRIVATE JUCE_VST3_CAN_REPLACE_VST2=0)
endif()



target_sources(CompassCompressor PRIVATE
    Core/InputConditioning.h
    Core/DetectorSplit.h
    Core/DetectorCore.h
    Core/HybridEnvelopeEngine.h
    Core/GainComputer.h
    Core/GainReductionStage.h
    Core/ParallelMixer.h
    Core/StereoLink.h
    Core/OutputStage.h
    Core/OversamplingAndSafety.h
    Core/CompressorPipeline.h
    PluginProcessor.cpp
    PluginProcessor.h
    PluginEditor.cpp
    PluginEditor.h
)

target_compile_definitions(CompassCompressor
    PRIVATE
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_VST2_REPLACEMENT=0
        JucePlugin_Build_VST2=0
        JUCE_VST2_REPLACEMENT=0
        JucePlugin_Build_VST2=0
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_SVG=1
)

target_link_libraries(CompassCompressor
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        juce::juce_recommended_lto_flags
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_audio_processors
        juce::juce_audio_basics
)
