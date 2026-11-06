# aa_add_plugin(<Target>
#     PRODUCT_NAME  "Duck"
#     PLUGIN_CODE   Duck          # unique 4 chars, at least one uppercase
#     [DESCRIPTION  "..."]
#     [IS_SYNTH FALSE]          # TRUE: an instrument (VST3 category Instrument|Synth, no audio input needed)
#     [NEEDS_MIDI_INPUT FALSE]  # TRUE gives the effect a MIDI input (e.g. notes that steer it)
#     [NEEDS_MIDI_OUTPUT FALSE] # TRUE gives it a MIDI output (e.g. an instrument's played notes, for recording)
#     [BUNDLE_ID    com.animalaudio.duck]
#     SOURCES       a.cpp b.cpp ...)
#
# Wraps juce_add_plugin with the Animal Audio defaults (manufacturer, VST3 only,
# Ableton-friendly category, shared libs linked, warnings on).
function(aa_add_plugin target)
    cmake_parse_arguments(AA "" "PRODUCT_NAME;PLUGIN_CODE;DESCRIPTION;IS_SYNTH;NEEDS_MIDI_INPUT;NEEDS_MIDI_OUTPUT;BUNDLE_ID;VERSION"
                          "SOURCES" ${ARGN})

    if(NOT AA_PRODUCT_NAME OR NOT AA_PLUGIN_CODE)
        message(FATAL_ERROR "aa_add_plugin(${target}): PRODUCT_NAME and PLUGIN_CODE are required")
    endif()
    if(NOT AA_DESCRIPTION)
        set(AA_DESCRIPTION "${AA_PRODUCT_NAME} by Animal Audio")
    endif()
    if(NOT AA_NEEDS_MIDI_INPUT)
        set(AA_NEEDS_MIDI_INPUT FALSE)
    endif()
    if(NOT AA_NEEDS_MIDI_OUTPUT)
        set(AA_NEEDS_MIDI_OUTPUT FALSE)
    endif()
    if(NOT AA_IS_SYNTH)
        set(AA_IS_SYNTH FALSE)
    endif()
    if(NOT AA_BUNDLE_ID)
        string(TOLOWER "${AA_PRODUCT_NAME}" _lower)
        string(REGEX REPLACE "[^a-z0-9]" "" _lower "${_lower}")
        set(AA_BUNDLE_ID "com.animalaudio.${_lower}")
    endif()

    # Instruments must say so, or Live files them under Audio Effects and gives them no MIDI track slot.
    if(AA_IS_SYNTH)
        set(_vst3_categories Instrument Synth)
    else()
        set(_vst3_categories Fx)
    endif()

    set(_formats VST3)
    if(AA_BUILD_STANDALONE)
        list(APPEND _formats Standalone)
    endif()

    # A local build is its own plugin to the host (name, bundle id and plugin code), so it sits beside the released
    # one in Live's browser instead of replacing or shadowing it. Plugin code: first three letters + 'L' ('K' if the
    # release code already ends in 'L').
    set(_product_name "${AA_PRODUCT_NAME}")
    set(_bundle_id "${AA_BUNDLE_ID}")
    set(_plugin_code "${AA_PLUGIN_CODE}")
    if(AA_LOCAL_BUILD)
        set(_product_name "${AA_PRODUCT_NAME} Local")
        set(_bundle_id "${AA_BUNDLE_ID}.local")
        string(SUBSTRING "${AA_PLUGIN_CODE}" 0 3 _code_head)
        string(SUBSTRING "${AA_PLUGIN_CODE}" 3 1 _code_tail)
        if(_code_tail STREQUAL "L")
            set(_plugin_code "${_code_head}K")
        else()
            set(_plugin_code "${_code_head}L")
        endif()
    endif()

    juce_add_plugin(${target}
        PRODUCT_NAME "${_product_name}"
        COMPANY_NAME "Animal Audio"
        COMPANY_WEBSITE "https://animal.audio"
        BUNDLE_ID "${_bundle_id}"
        PLUGIN_MANUFACTURER_CODE AnAu
        PLUGIN_CODE ${_plugin_code}
        FORMATS ${_formats}
        VST3_CATEGORIES ${_vst3_categories}
        IS_SYNTH ${AA_IS_SYNTH}
        NEEDS_MIDI_INPUT ${AA_NEEDS_MIDI_INPUT}
        NEEDS_MIDI_OUTPUT ${AA_NEEDS_MIDI_OUTPUT}
        DESCRIPTION "${AA_DESCRIPTION}"
        COPY_PLUGIN_AFTER_BUILD ${AA_COPY_PLUGINS_AFTER_BUILD})

    target_sources(${target} PRIVATE ${AA_SOURCES})

    if(NOT AA_VERSION)
        set(_version_file "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
        if(NOT EXISTS "${_version_file}")
            message(FATAL_ERROR "aa_add_plugin(${target}): missing ${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
        endif()
        file(READ "${_version_file}" AA_VERSION)
        string(STRIP "${AA_VERSION}" AA_VERSION)
    endif()
    aa_attach_plugin_build_info(${target} "${AA_VERSION}")

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_REPORT_APP_USAGE=0)

    target_link_libraries(${target}
        PRIVATE
            animalaudio::ui
            animalaudio::core
            animalaudio::dsp
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()
