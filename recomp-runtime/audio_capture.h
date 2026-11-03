#ifndef DOAXBV_RECOMP_AUDIO_CAPTURE_H
#define DOAXBV_RECOMP_AUDIO_CAPTURE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The game's sound while a video records (docs/free-camera.md): the audio
   output hands its final mix here (SDL's postmix callback, or an effect on
   XAudio2's mastering voice), and the video recorder takes it a frame at a
   time. Kept as stereo float at the output's own rate, in a ring of two
   seconds; nothing is kept unless capturing is on. */

/* Starts (clearing what was kept) or stops capturing. */
void recomp_audio_capture_set(bool on);
bool recomp_audio_capture_on(void);
/* The audio thread: `count` frames of `channels` interleaved floats at
   `rate`; the first two channels are kept. */
void recomp_audio_capture_feed(const float *frames, uint32_t count, uint32_t channels, uint32_t rate);
/* The output's rate, or 0 before any sound came. */
uint32_t recomp_audio_capture_rate(void);
/* Takes `count` stereo frames for one video frame, oldest first, padding
   with silence when fewer came. More than half a second waiting is dropped
   down to a frame's worth, so the sound keeps up with the picture. Returns
   how many were real. */
uint32_t recomp_audio_capture_take(float *stereo, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif
