#include "audio_capture.h"

#include <string.h>

#ifdef _WIN32
#include <windows.h>
static SRWLOCK lock_object = SRWLOCK_INIT;
static void lock(void) { AcquireSRWLockExclusive(&lock_object); }
static void unlock(void) { ReleaseSRWLockExclusive(&lock_object); }
#else
#include <pthread.h>
static pthread_mutex_t lock_object = PTHREAD_MUTEX_INITIALIZER;
static void lock(void) { pthread_mutex_lock(&lock_object); }
static void unlock(void) { pthread_mutex_unlock(&lock_object); }
#endif

/* Two seconds at the highest rate an output is likely to run. */
enum { RING_FRAMES = 2u * 192000u };

static float ring[RING_FRAMES * 2u];
static uint32_t head, kept, rate;
static volatile bool capturing;

void recomp_audio_capture_set(bool on)
{
    lock();
    capturing = on;
    head = kept = 0u;
    unlock();
}

bool recomp_audio_capture_on(void)
{
    return capturing;
}

void recomp_audio_capture_feed(const float *frames, uint32_t count, uint32_t channels, uint32_t frame_rate)
{
    if (!capturing || frames == NULL || channels == 0u) return;
    lock();
    rate = frame_rate;
    for (uint32_t i = 0u; i < count; ++i) {
        const float left = frames[i * channels], right = channels > 1u ? frames[i * channels + 1u] : left;
        const uint32_t at = (head + kept) % RING_FRAMES;
        ring[at * 2u] = left;
        ring[at * 2u + 1u] = right;
        if (kept < RING_FRAMES) ++kept;
        else head = (head + 1u) % RING_FRAMES;
    }
    unlock();
}

uint32_t recomp_audio_capture_rate(void)
{
    lock();
    const uint32_t value = rate;
    unlock();
    return value;
}

uint32_t recomp_audio_capture_take(float *stereo, uint32_t count)
{
    lock();
    /* Behind by more than half a second: catch up to a frame's worth. */
    if (rate != 0u && kept > rate / 2u + count) {
        const uint32_t drop = kept - count;
        head = (head + drop) % RING_FRAMES;
        kept = count;
    }
    const uint32_t real = kept < count ? kept : count;
    for (uint32_t i = 0u; i < real; ++i) {
        const uint32_t at = (head + i) % RING_FRAMES;
        stereo[i * 2u] = ring[at * 2u];
        stereo[i * 2u + 1u] = ring[at * 2u + 1u];
    }
    head = (head + real) % RING_FRAMES;
    kept -= real;
    unlock();
    if (real < count) memset(stereo + real * 2u, 0, (size_t)(count - real) * 2u * sizeof(float));
    return real;
}
