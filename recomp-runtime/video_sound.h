#ifndef DOAXBV_RECOMP_VIDEO_SOUND_H
#define DOAXBV_RECOMP_VIDEO_SOUND_H

/* The sound of a recorded video (docs/free-camera.md), for every presenter's
   recorder: while it records, each frame written to the video takes its
   1/60 s of the game's final mix (audio_capture.h) into a raw file beside
   it, so sound and picture stay frame for frame together; when it stops,
   ffmpeg puts the sound into the video without encoding the picture again.
   With no sound captured (muted, no output device) the video is left as it
   is. C++, header only. */

#include "audio_capture.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

struct VideoSound {
    FILE *raw = nullptr;
    std::string raw_path;
    uint32_t rate = 0;
    double owed = 0.0;
    uint64_t real_frames = 0;

    void start(const std::string &video_path)
    {
        finishWithout();
        raw_path = video_path + ".sound.raw";
        raw = std::fopen(raw_path.c_str(), "wb");
        rate = 0;
        owed = 0.0;
        real_frames = 0;
        if (raw != nullptr) recomp_audio_capture_set(true);
    }

    // One video frame written: its share of the sound.
    void frame()
    {
        if (raw == nullptr) return;
        if (rate == 0) rate = recomp_audio_capture_rate();
        if (rate == 0) return; // no sound yet: the video starts silent
        owed += double(rate) / 60.0;
        const uint32_t count = uint32_t(owed);
        owed -= count;
        if (count == 0) return;
        std::vector<float> stereo(size_t(count) * 2u);
        real_frames += recomp_audio_capture_take(stereo.data(), count);
        std::fwrite(stereo.data(), sizeof(float), stereo.size(), raw);
    }

    // Stops without adding the sound (the video failed, or there was none).
    void finishWithout()
    {
        if (raw != nullptr) {
            recomp_audio_capture_set(false);
            std::fclose(raw);
            raw = nullptr;
            std::remove(raw_path.c_str());
        }
    }

    static std::string quoted(const std::string &text)
    {
#ifdef _WIN32
        return "\"" + text + "\"";
#else
        std::string out = "'";
        for (char c : text) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
        return out + "'";
#endif
    }

    // Puts `from` in place of `to` in one step; the silent video stays if it fails.
    static bool replace(const std::string &from, const std::string &to)
    {
#ifdef _WIN32
        return MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
        return std::rename(from.c_str(), to.c_str()) == 0;
#endif
    }

    /* After the video's ffmpeg finished well: muxes the sound in, replacing
       the video file. True when the video now has sound. */
    bool finish(const std::string &ffmpeg, const std::string &video_path, bool video_ok)
    {
        if (raw == nullptr) return false;
        recomp_audio_capture_set(false);
        std::fclose(raw);
        raw = nullptr;
        bool done = false;
        if (video_ok && rate != 0 && real_frames != 0) {
            const std::string with = video_path + ".sound.mp4";
            std::string command = quoted(ffmpeg) + " -y -loglevel error -i " + quoted(video_path) +
                " -f f32le -ar " + std::to_string(rate) + " -ac 2 -i " + quoted(raw_path) +
                " -c:v copy -c:a aac -b:a 192k -shortest " + quoted(with);
#ifdef _WIN32
            command = "\"" + command + "\""; // cmd.exe drops the outer quotes
#endif
            if (std::system(command.c_str()) == 0 && replace(with, video_path)) {
                done = true;
            } else {
                std::remove(with.c_str());
                std::fprintf(stderr, "recomp video: the sound could not be added; the video is silent\n");
            }
        }
        std::remove(raw_path.c_str());
        return done;
    }
};

#endif
