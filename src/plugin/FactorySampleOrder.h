#pragma once
#include <cstddef>
#include <string_view>

namespace bsl {
// The extracted library contains resources the original does not expose in its
// factory picker. Keep them loadable by existing states and custom sample import.
inline bool hiddenFactorySample(std::size_t pad, std::size_t layer, std::string_view filename)
{
    if (layer != 0) return false;
    if (pad == 0)
        return filename == "SRC-Syn_Inst-BD_TYPE-Body_KIT-5_NAME-Chad Thud.flac";
    if (pad == 3)
        return filename == "SRC-808_INST-Clap_TYPE-Hit_NAME-GR8 Clap.flac";
    if (pad == 4)
        return filename == "SRC-808_INST-HH_TYPE-Closed_NAME-GR8 Hat 2.flac";
    if (pad == 14 || pad == 15)
        return filename == "SRC-ND_INST-FX_TYPE-Zap_NAME-Zip.flac"
            || filename == "SRC-ND_INST-FX3_TYPE-Foley_NAME-AlienCricket.flac"
            || filename == "SRC-ND_INST-FX3_TYPE-Foley_NAME-Samurai.flac";
    return false;
}

// Original Windows sample-menu order, captured 2026-11-24.
// Unlisted samples remain available after the reference entries.
inline std::size_t factorySampleRank(std::size_t pad, std::size_t layer, std::string_view name)
{
    constexpr std::size_t unlisted = 10000;
    if (pad == 0 && layer == 0)
    {
        static constexpr std::string_view names[] {
            "GR8 Body",
            "DW Dirt",
            "DW Sub",
            "Akustick",
            "Belly",
            "DatOne",
            "Hollow",
            "Jolly",
            "LongTone",
            "Snub Sub",
            "Stinky Kick",
            "Timpanoise",
            "UglyNice",
            "Destructo",
            "Messy",
            "OverJoy",
            "Heart",
            "Knock Out",
        };
        for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if (name == names[i]) return i;
        return unlisted;
    }
    if (pad == 0 && layer == 1)
    {
        static constexpr std::string_view names[] {
            "SvN Kick",
            "GR8 Hit",
            "DStroy",
            "DW Hit",
            "Click",
            "Gonk",
            "Impact",
            "Musikick",
            "Paper",
            "SteelBanter",
            "Stubby",
            "Ungh",
            "Hoppy",
            "Tick",
            "SnapNsub",
            "Boozh",
            "Bop",
            "Yoni's Kck 1",
            "Yoni's Kck 2",
            "Yoni's Kck 3",
            "Yoni's Kck 4",
            "Yoni's Kck 5",
        };
        for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if (name == names[i]) return i;
        return unlisted;
    }
    if (pad == 1 && layer == 0)
    {
        static constexpr std::string_view names[] {
            "SvN 1",
            "GR8 Tone",
            "DW Dist",
            "DW Fizz",
            "FatFish",
            "Pico",
            "TuneThis",
            "Dull",
            "Snappy",
        };
        for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if (name == names[i]) return i;
        return unlisted;
    }
    if (pad == 1 && layer == 1)
    {
        static constexpr std::string_view names[] {
            "SvN 2",
            "GR8 Fizz",
            "DW Hit",
            "Eish",
            "Housio",
            "OliveOil",
            "Oomph",
            "Pico",
            "Springy",
            "Swifty",
            "Clappy",
            "Middy",
            "Shorty",
            "Grunt",
        };
        for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if (name == names[i]) return i;
        return unlisted;
    }
    if (pad == 2 && layer == 0)
    {
        static constexpr std::string_view names[] {
            "SvN Rim",
            "GR8 Rim",
            "Acousrim",
            "Dry Flam",
            "Sticks",
            "Wet Flam",
            "Wet Sticks",
            "WoodenMouth",
            "RimBoss",
            "FunkyRim",
            "The Rim",
            "Rim Jab",
        };
        for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if (name == names[i]) return i;
        return unlisted;
    }
    if (pad == 3 && layer == 0)
    {
        static constexpr std::string_view names[] {
            "SvN Clap",
            "ChoirClap",
            "Crispy Clap",
            "ProClapper",
            "Shlap!",
            "ShotTheDeputyToo",
            "Slap",
            "Smack",
            "CCClap",
            "Shmack",
            "The Clap",
            "Happy McClappy",
            "70s Clap",
        };
        for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if (name == names[i]) return i;
        return unlisted;
    }
    if (pad == 4 && layer == 0)
    {
        static constexpr std::string_view names[] {
            "SvN Hat 1",
            "SvN Hat 2",
            "GR8 Hat",
            "DW Hat",
            "Electro Tight",
            "MoneyMaka",
            "SpringHat",
            "TishHat",
            "Fashion Hat 2",
            "Fashion Hat",
            "Jiggley Hat",
            "Jingley Hat",
            "MetallicTing",
            "JuicyHat",
            "CuteHat",
        };
        for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if (name == names[i]) return i;
        return unlisted;
    }
    if (pad == 5 && layer == 0)
    {
        static constexpr std::string_view names[] {
            "SvNHat Open",
            "GR8 OpenHat",
            "DW OpenHat",
            "DW SemiOpen",
            "Pedal Hat",
            "Sneaky",
            "Sweetness",
            "Bish Pleez",
            "OhGreat",
        };
        for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if (name == names[i]) return i;
        return unlisted;
    }
    if (pad == 6 && layer == 0)
    {
        static constexpr std::string_view names[] {
            "SvN Crash",
            "GR8 Crash",
            "GR8 Ride",
            "Bella",
            "DW LeftCrash",
            "DW RightCrash",
            "Splash",
            "Woosh",
            "WashAway",
            "SloMo Crash",
        };
        for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if (name == names[i]) return i;
        return unlisted;
    }
    if (pad == 7 && layer == 0)
    {
        static constexpr std::string_view names[] {
            "SvN Ride",
            "Bella Bell",
            "Rider",
            "Skinny",
            "Ride On",
            "DigiRide",
            "Dry Ride",
            "Tiss",
            "Storm Rider",
        };
        for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if (name == names[i]) return i;
        return unlisted;
    }
    if (pad == 8 && layer == 0)
    {
        static constexpr std::string_view names[] {
            "SvN Crash",
            "GR8 Crash",
            "GR8 Ride",
            "Bella",
            "DW LeftCrash",
            "DW RightCrash",
            "Splash",
            "Woosh",
            "WashAway",
            "SloMo Crash",
        };
        for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if (name == names[i]) return i;
        return unlisted;
    }
    if (pad == 9 && layer == 0)
    {
        static constexpr std::string_view names[] {
            "SvN Tom Hi",
            "GR8 Conga 1",
            "GR8 Tom Hi",
            "Bamboo 1",
            "Bouk 1",
            "LoseYoCoins 1",
            "Timba 1",
            "WatUDoing 1",
            "Zipper Tom 1",
            "Dzh Tom 1",
            "Zhong 1",
            "Alien Sneeze 1",
            "Congrats 1",
            "Hutzpa 1",
            "TunaTom 1",
            "A Oct Slap",
            "E Oct Palm",
            "E Oct Slap",
        };
        for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if (name == names[i]) return i;
        return unlisted;
    }
    if (pad == 10 && layer == 0)
    {
        static constexpr std::string_view names[] {
            "SvN Tom Mid",
            "GR8 Conga 2",
            "GR8 Tom Mid",
            "DW DistTom 1",
            "DW Tom 2",
            "Bamboo 2",
            "Bouk 2",
            "LoseYoCoins 2",
            "Timba 2",
            "WatUDoing 2",
            "Zipper Tom 2",
            "Dzh Tom 2",
            "Zhong 2",
            "Alien Sneeze 2",
            "Congrats 2",
            "Hutzpa 2",
            "TunaTom 2",
            "A Palm",
            "B Slap",
            "E Slap",
        };
        for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if (name == names[i]) return i;
        return unlisted;
    }
    if (pad == 11 && layer == 0)
    {
        static constexpr std::string_view names[] {
            "SvN Tom Low",
            "GR8 Conga 3",
            "GR8 Tom Low",
            "DW DistTom 2",
            "DW Tom 3",
            "Bamboo 3",
            "Bouk 3",
            "LoseYoCoins Tom 3",
            "Timba 3",
            "WatUDoing 3",
            "Zipper Tom 3",
            "Dzh Tom 3",
            "Zhong 3",
            "Alien Sneeze 3",
            "Hutzpa 3",
            "TunaTom 3",
            "A Slap",
            "E Palm",
            "E Slap",
        };
        for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if (name == names[i]) return i;
        return unlisted;
    }
    if (pad == 12 && layer == 0)
    {
        static constexpr std::string_view names[] {
            "SvN Tambo",
            "GR8 Maracas",
            "Bells Hit",
            "Bells Turn",
            "CloseUp Tambo",
            "DropYoKeys",
            "HealthyShake",
            "Janitor",
            "ShakeMeDown",
            "Shaker Hit",
            "Shaker Run",
            "Shaker Sizzle",
            "TamboMean",
            "TamboSeizure",
            "Tambon",
            "Tickles",
            "Klak",
            "PFFT",
            "ShekShek",
            "Salt",
        };
        for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if (name == names[i]) return i;
        return unlisted;
    }
    if (pad == 13 && layer == 0)
    {
        static constexpr std::string_view names[] {
            "GR8 Beep",
            "GR8 Ding",
            "GoatBell",
            "Ping",
            "QTClave",
            "TuneYoMind",
            "WoodBlox",
            "Zhbeezh",
            "ShortBeep",
            "MateOhMate",
            "3Angles",
            "WoodBlock",
            "WoodierBlock",
        };
        for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if (name == names[i]) return i;
        return unlisted;
    }
    if (pad == 14 && layer == 0)
    {
        static constexpr std::string_view names[] {
            "Finger Snap",
            "Single Snap 1",
            "Single Snap 2",
            "SnareRoll",
            "BRBRBR",
            "Bouncy Orb",
            "CaveSnap",
            "Chorda",
            "ChordaB",
            "ChordaC",
            "DingForNextLine",
            "Gazorp",
            "Hurtz",
            "KadubRaw",
            "MonkMealTime",
            "Oyo",
            "Peuuhhh",
            "Pew",
            "Phasers2Stun",
            "RoboScream",
            "SeriousBizz",
            "SickRobogy",
            "Springy Bass",
            "SpritualBing",
            "Stringy",
            "SuppaTime",
            "Wep",
            "Why",
            "Bm7",
            "Crackle",
            "F#m7",
            "TearDrop",
            "ConfuseMe",
            "Error",
            "Droplet",
            "Mistake",
            "Quasar",
            "Cave Ice",
            "Plucked",
            "SpaceDucks",
            "Horrible",
            "Looney",
            "Pull Mute",
            "Slap Mute",
        };
        for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if (name == names[i]) return i;
        return unlisted;
    }
    if (pad == 15 && layer == 0)
    {
        static constexpr std::string_view names[] {
            "Finger Snap",
            "Single Snap 1",
            "Single Snap 2",
            "SnareRoll",
            "BRBRBR",
            "Bouncy Orb",
            "CaveSnap",
            "Chorda",
            "ChordaB",
            "ChordaC",
            "DingForNextLine",
            "Gazorp",
            "Hurtz",
            "KadubRaw",
            "MonkMealTime",
            "Oyo",
            "Peuuhhh",
            "Pew",
            "Phasers2Stun",
            "RoboScream",
            "SeriousBizz",
            "SickRobogy",
            "Springy Bass",
            "SpritualBing",
            "Stringy",
            "SuppaTime",
            "Wep",
            "Why",
            "Bm7",
            "Crackle",
            "F#m7",
            "TearDrop",
            "ConfuseMe",
            "Error",
            "Droplet",
            "Mistake",
            "Quasar",
            "Cave Ice",
            "Plucked",
            "SpaceDucks",
            "Horrible",
            "Looney",
            "Pull Mute",
            "Slap Mute",
        };
        for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if (name == names[i]) return i;
        return unlisted;
    }
    return unlisted;
}
} // namespace bsl
