/**
 * @file
 * @brief Private machine-readable FFmpeg container/codec/profile qualification set.
 * @author Roman Glaz
 * @copyright © 2026, <vokerlee@gmail.com>
 */

#include "qualification.hpp"

#include <algorithm>
#include <array>

namespace wiola::codec::ffmpeg {

namespace {

constexpr std::array qualified{
    Qualification{"Ogg Vorbis/Opus",           "ogg",                     "vorbis",    ""  },
    Qualification{"Ogg Vorbis/Opus",           "ogg",                     "opus",      ""  },
    Qualification{"MP4/M4A AAC-LC/ALAC",       "mov,mp4,m4a,3gp,3g2,mj2", "aac",       "LC"},
    Qualification{"MP4/M4A AAC-LC/ALAC",       "mov,mp4,m4a,3gp,3g2,mj2", "alac",      ""  },
    Qualification{"ADTS AAC-LC",               "aac",                     "aac",       "LC"},
    Qualification{"AIFF PCM",                  "aiff",                    "pcm_s16be", ""  },
    Qualification{"AIFC PCM",                  "aiff",                    "pcm_s16le", ""  },
    Qualification{"AU PCM",                    "au",                      "pcm_s32be", ""  },
    Qualification{"CAF PCM",                   "caf",                     "pcm_f64le", ""  },
    Qualification{"CAF PCM",                   "caf",                     "pcm_s16le", ""  },
    Qualification{"Wave64 PCM",                "w64",                     "pcm_s24le", ""  },
    Qualification{"RF64 PCM",                  "wav",                     "pcm_f32le", ""  },
    Qualification{"WavPack",                   "wv",                      "wavpack",   ""  },
    Qualification{"Monkey's Audio",            "ape",                     "ape",       ""  },
    Qualification{"TTA",                       "tta",                     "tta",       ""  },
    Qualification{"ASF WMAv2",                 "asf",                     "wmav2",     ""  },
    Qualification{"Matroska/WebM Vorbis/Opus", "matroska,webm",           "vorbis",    ""  },
    Qualification{"Matroska/WebM Vorbis/Opus", "matroska,webm",           "opus",      ""  },
    Qualification{"WAV PCM",                   "wav",                     "pcm_s16le", ""  },
};

} // namespace

std::span<const Qualification> qualifications() noexcept
{
    return qualified;
}

bool is_qualified(std::string_view container, std::string_view codec,
    std::string_view profile) noexcept
{
    return std::ranges::any_of(qualified, [&](const Qualification& candidate) {
        return candidate.container == container && candidate.codec == codec &&
            candidate.profile == profile;
    });
}

} // namespace wiola::codec::ffmpeg
