set(COSMOS_SOURCES
    Source/CosmosEditor.cpp
    Source/CosmosProcessor.cpp
    Source/dsp/CosmosEngine.cpp
    Source/dsp/PhraseLooper.cpp
    Source/ui/CosmosMotif.cpp
    Source/ui/CosmosTheme.cpp
    Source/ui/CosmosWidgets.cpp)

aa_add_plugin(Cosmos
    PRODUCT_NAME "Cosmos"
    PLUGIN_CODE Cosm
    DESCRIPTION "Granular looper: micro loops, granules, glitch and multidelay with hold, phrase looper and reverb"
    SOURCES ${COSMOS_SOURCES})

target_include_directories(Cosmos PRIVATE Source)

if(AA_BUILD_TESTS)
    # Set AA_SCREENSHOT_DIR to also write the headless UI render as a PNG.
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _cosmos_version)
    string(STRIP "${_cosmos_version}" _cosmos_version)
    add_executable(CosmosTests tests/CosmosTests.cpp ${COSMOS_SOURCES})
    target_include_directories(CosmosTests PRIVATE Source)
    aa_attach_plugin_build_info(CosmosTests "${_cosmos_version}")
    target_link_libraries(CosmosTests PRIVATE
        animalaudio::testing
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    catch_discover_tests(CosmosTests TEST_PREFIX "cosmos." PROPERTIES RESOURCE_LOCK native_display)
endif()
