set(DOLPHINFLOW_SOURCES
    Source/DolphinFlowEditor.cpp
    Source/DolphinFlowProcessor.cpp
    Source/dsp/DolphinFlowEngine.cpp
    Source/ui/DolphinFlowMotif.cpp
    Source/ui/DolphinFlowTheme.cpp)

aa_add_plugin(DolphinFlow
    PRODUCT_NAME "DolphinFlow"
    PLUGIN_CODE Dlph
    DESCRIPTION "Keyboard chord instrument where every note glides into the next chord"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    SOURCES ${DOLPHINFLOW_SOURCES})

target_include_directories(DolphinFlow PRIVATE Source)

if(AA_BUILD_TESTS)
    # Set AA_SCREENSHOT_DIR to also write the headless UI render as a PNG.
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _dolphinflow_version)
    string(STRIP "${_dolphinflow_version}" _dolphinflow_version)
    add_executable(DolphinFlowTests tests/DolphinFlowTests.cpp ${DOLPHINFLOW_SOURCES})
    target_include_directories(DolphinFlowTests PRIVATE Source)
    aa_attach_plugin_build_info(DolphinFlowTests "${_dolphinflow_version}")
    target_link_libraries(DolphinFlowTests PRIVATE
        animalaudio::testing
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    catch_discover_tests(DolphinFlowTests TEST_PREFIX "dolphinflow." PROPERTIES RESOURCE_LOCK native_display)
endif()
