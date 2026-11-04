#pragma once

namespace cosmos::dsp
{
/** The eleven effects, grouped into four families. The index is the `mode` parameter (saved state). */
enum class Mode : int
{
    Mosaic = 0,
    Sequence,
    Glide,
    Haze,
    Tunnel,
    Strum,
    Blocks,
    Interrupt,
    Arp,
    Pattern,
    Warp,
    Count
};

enum class Family : int
{
    MicroLoop = 0,
    Granules,
    Glitch,
    MultiDelay,
    Count
};

constexpr int kNumModes = static_cast<int>(Mode::Count);
constexpr int kNumFamilies = static_cast<int>(Family::Count);
constexpr int kNumVariations = 4;

/** TIME: clock steps per beat. Index is the `time` parameter. */
constexpr int kNumTimes = 6;
constexpr double kTimeMultipliers[kNumTimes] = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0};
constexpr const char* kTimeLabels[kNumTimes] = {"1/4", "1/2", "1X", "2X", "4X", "8X"};
constexpr int kDefaultTime = 2;

inline Mode modeFromIndex(int index) noexcept
{
    return static_cast<Mode>(index < 0 ? 0 : index >= kNumModes ? kNumModes - 1 : index);
}

inline Family familyOf(Mode mode) noexcept
{
    switch (mode)
    {
    case Mode::Mosaic:
    case Mode::Sequence:
    case Mode::Glide:
        return Family::MicroLoop;
    case Mode::Haze:
    case Mode::Tunnel:
    case Mode::Strum:
        return Family::Granules;
    case Mode::Blocks:
    case Mode::Interrupt:
    case Mode::Arp:
        return Family::Glitch;
    case Mode::Pattern:
    case Mode::Warp:
    case Mode::Count:
        break;
    }
    return Family::MultiDelay;
}

inline const char* modeName(Mode mode) noexcept
{
    static constexpr const char* names[kNumModes] = {"Mosaic", "Sequence",  "Glide", "Haze",    "Tunnel", "Strum",
                                                     "Blocks", "Interrupt", "Arp",   "Pattern", "Warp"};
    const int index = static_cast<int>(mode);
    return index >= 0 && index < kNumModes ? names[index] : "?";
}

inline const char* familyName(Family family) noexcept
{
    static constexpr const char* names[kNumFamilies] = {"MICRO LOOP", "GRANULES", "GLITCH", "MULTIDELAY"};
    const int index = static_cast<int>(family);
    return index >= 0 && index < kNumFamilies ? names[index] : "?";
}

/** What ACTIVITY does in each mode (UI hint). */
inline const char* activityMeaning(Mode mode) noexcept
{
    switch (mode)
    {
    case Mode::Mosaic:
        return "LAYERS";
    case Mode::Sequence:
        return "STEP DENSITY";
    case Mode::Glide:
        return "GLIDE DEPTH";
    case Mode::Haze:
        return "GRAIN DENSITY";
    case Mode::Tunnel:
        return "DRONE DENSITY";
    case Mode::Strum:
        return "STRUM SPEED";
    case Mode::Blocks:
        return "SHUFFLE";
    case Mode::Interrupt:
        return "INTERRUPTIONS";
    case Mode::Arp:
        return "ARP SPEED";
    case Mode::Pattern:
        return "TAPS";
    case Mode::Warp:
        return "WARBLE";
    case Mode::Count:
        break;
    }
    return "";
}

/** One line on what each mode does with the variations (UI). */
inline const char* modeBlurb(Mode mode) noexcept
{
    switch (mode)
    {
    case Mode::Mosaic:
        return "Stacks micro loops at different speeds";
    case Mode::Sequence:
        return "Steps through pitched loop slices";
    case Mode::Glide:
        return "Loops that slide in pitch";
    case Mode::Haze:
        return "A dense cloud of short grains";
    case Mode::Tunnel:
        return "Long overlapping grains, a drone";
    case Mode::Strum:
        return "Grains strummed as a chord";
    case Mode::Blocks:
        return "Chops and reorders the beat";
    case Mode::Interrupt:
        return "Stutters break into the signal";
    case Mode::Arp:
        return "Slices played as an arpeggio";
    case Mode::Pattern:
        return "Rhythmic multi-tap echoes";
    case Mode::Warp:
        return "Echoes that bend and shift";
    case Mode::Count:
        break;
    }
    return "";
}
} // namespace cosmos::dsp
