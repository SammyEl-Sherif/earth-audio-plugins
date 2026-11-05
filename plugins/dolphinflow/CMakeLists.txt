set(SWAN_SOURCES
    Source/SwanEditor.cpp
    Source/SwanProcessor.cpp
    Source/dsp/SwanEngine.cpp
    Source/ui/SwanMotif.cpp
    Source/ui/SwanTheme.cpp)

aa_add_plugin(Swan
    PRODUCT_NAME "Swan"
    PLUGIN_CODE Swan
    DESCRIPTION "Keyboard chord instrument where every note glides into the next chord"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    SOURCES ${SWAN_SOURCES})

target_include_directories(Swan PRIVATE Source)

if(AA_BUILD_TESTS)
    # Set AA_SCREENSHOT_DIR to also write the headless UI render as a PNG.
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _swan_version)
    string(STRIP "${_swan_version}" _swan_version)
    add_executable(SwanTests tests/SwanTests.cpp ${SWAN_SOURCES})
    target_include_directories(SwanTests PRIVATE Source)
    aa_attach_plugin_build_info(SwanTests "${_swan_version}")
    target_link_libraries(SwanTests PRIVATE
        animalaudio::testing
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    catch_discover_tests(SwanTests TEST_PREFIX "swan." PROPERTIES RESOURCE_LOCK native_display)
endif()
