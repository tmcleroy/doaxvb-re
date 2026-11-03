#include "free_camera_adapter.h"
#include "free_camera.h"
#include "input_host_win32.h"
#include "scene_freeze_adapter.h"
#include "actor_pose_adapter.h"
#include "body_morph_adapter.h"
#include "suit_switch_adapter.h"
#include "viewer_menu_adapter.h"
#include "overlay_text.h"
#include "screenshot.h"
#include "camera_settings.h"
#include "camera_path.h"
#include "slow_motion.h"
#include "viewer_menu_adapter.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef RECOMP_FULL_PROGRAM
#include "animation_probe.h"
#include "d3d_draw_adapter.h"
#include "camera_store.h"
#include "kernel_abi.h"
#include "d3d_frame_adapter.h"
#include "game_scheduler.h"

/* Free camera: RECOMP_FREE_CAMERA=1 lets F1, or clicking the right stick,
   take the game's camera and fly it. It used to take BACK with the click,
   but the game saw that BACK first and backed out of its menus.

   The game keeps five camera objects of 0xAE0 bytes from 0x41A800. Each
   frame 0x00023B10 builds one from the object in EAX: it reads the eye at
   +0x180, the target at +0x190 and the vertical field of view in radians at
   +0x1B0, and writes the view and projection matrices at +0x50 and +0x90 that
   the frame's draws then use. The hook puts the free camera's eye, target
   and field of view in place for that build and leaves them there: the game
   culls each part of a character against the eye and target it finds in the
   object, so putting its own back made parts at the screen's edges vanish.
   When the free camera is put away the game's camera logic takes the object
   over again.

   Which object holds the main view depends on the scene: the volleyball
   demo builds only the first, the island map builds the first for the map
   and others for the portraits, and the hotel room builds the first for a
   view nothing shows and the third for the room. The free camera takes the
   object whose view the most draws used in the last frame (each draw's view
   transform is matched against each object's view matrix), again whenever
   the one it flies stops being built, and C or X moves it to the next
   object the game built in the last frame. Choosing by the order of builds
   and draws instead lost the view of the poolside's rest and hopping game.

   P or START freezes the scene while the camera flies (scene_freeze_adapter.c),
   H or the left shoulder hides the 2D overlay of a frozen scene, . or the
   right shoulder moves it on one frame, and putting the camera away thaws
   it. The game polls the controller from a task the freeze skips, so while
   frozen the freeze's frame hook samples it here once a frame, in any scene,
   so the scene can always be thawed.

   [ and ] or the d-pad's left and right play the previous or next animation
   clip on a character and hold it (actor_pose_adapter.c); \ or the left
   stick's click picks the next character and turns the camera to face her;
   - and = or BACK with the d-pad's down and up put her previous or next
   swimsuit on her (suit_switch_adapter.c). Changing the clip of a frozen
   scene steps it one frame so the new pose shows. Putting the camera away
   hands the animation back to the game.

   Tab, or BACK pressed and let go on its own, opens the camera settings
   while flying (camera_settings.h): up and down pick a row, left and right
   change it, and the presenter draws the effects (camera_effects.h) on the
   window and screenshots. While the settings are open the arrows and the
   d-pad belong to them. */
enum {
    BUILD_CAMERA = 0x00023B10u,
    CAMERA_FIRST = 0x0041A800u,
    CAMERA_STRIDE = 0x0AE0u,
    CAMERA_COUNT = 5u,
    CAMERA_VIEW = 0x50u,
    CAMERA_PROJECTION = 0x90u,
    SCENE_VIEW = 0x004D6F20u,
    SCENE_PROJECTION = 0x004D95F0u,
    CAMERA_EYE = 0x180u,
    CAMERA_TARGET = 0x190u,
    CAMERA_FOV = 0x1B0u,
    TRACE_LINES = 600u,
    XBOX_DPAD_UP = 0x0001u,
    XBOX_DPAD_DOWN = 0x0002u,
    XBOX_DPAD_LEFT = 0x0004u,
    XBOX_DPAD_RIGHT = 0x0008u,
    XBOX_START = 0x0010u,
    XBOX_LEFT_THUMB = 0x0040u,
    XBOX_BACK = 0x0020u,
    XBOX_RIGHT_THUMB = 0x0080u,
    ANALOG_A = 0u,
    ANALOG_B = 1u,
    ANALOG_X = 2u,
    ANALOG_Y = 3u,
    ANALOG_BLACK = 4u,
    ANALOG_WHITE = 5u,
    ANALOG_LTRIG = 6u,
    ANALOG_RTRIG = 7u,
    ANALOG_PRESSED = 0x20u,
};

static const float STICK_DEADZONE = 0.15f;
/* Holding a clip key: the first repeat after this long, then one every
   CLIP_REPEAT_MS, moving 3 clips at a time after CLIP_FASTER_AFTER_MS and 10
   after CLIP_FASTEST_AFTER_MS. */
enum {
    CLIP_REPEAT_DELAY_MS = 350,
    CLIP_REPEAT_MS = 60,
    CLIP_FASTER_AFTER_MS = 1500,
    CLIP_FASTEST_AFTER_MS = 3500,
};
/* At a field of view of one radian. */
static const float MOUSE_RADIANS_PER_PIXEL = 0.004f;
/* Framing a character: this far from her hips, raised a little, at this
   field of view. */
static const float FRAME_DISTANCE = 2.5f;
static const float FRAME_LIFT = 0.15f;
static const float FRAME_FOV = 0.75f;
/* Turntable: radians a second, a turn in about 18 seconds. */
static const float TURNTABLE_RATE = 0.35f;
/* Hand-held sway at Light: radians (about a fifth of a degree). */
static const float SWAY_ANGLE = 0.0035f;
/* Moving her: world units and radians a second at full stick. */
static const float MOVE_HER_SPEED = 1.5f;
static const float MOVE_HER_TURN = 1.8f;
/* How fast the tracked point catches up with her, a second. */
static const float TRACK_RATE = 6.0f;
/* The free camera steps once per presented frame. */
static const float FRAME_SECONDS = 1.0f / 60.0f;

/* A key or button held down to move through a list: a press moves one
   place, holding keeps moving, faster the longer it is held. */
typedef struct HeldRepeat {
    int direction;
    uint64_t since, next;
} HeldRepeat;

static struct {
    int enabled;
    RecompInputSampleSource source;
    bool active, started;
    bool toggle_held, reset_held, cycle_held, freeze_held, overlay_held;
    bool actor_held, shot_held;
    HeldRepeat clip_held;
    HeldRepeat suit_held, step_repeat;
    /* Face the chosen character at the next camera build; frame_pending
       also moves close to her. */
    bool aim_pending, frame_pending;
    /* The model viewer has the camera (recomp_free_camera_overlay). */
    bool viewer;
    /* The view the model viewer first framed her in, which reset returns to. */
    RecompFreeCamera home;
    bool home_saved, save_home;
    /* H's clean view also hides the runtime's own overlay. */
    bool panel_hidden;
    /* The hint lists controller buttons once a controller was last used,
       and from the start on a Steam Deck; keys once a key was. */
    int controller_hints;
    uint32_t slot;
    /* Objects built in the frame being drawn and in the one before it, and
       the frame the game last built one itself. */
    uint32_t built_frame, built, built_before, game_built_frame;
    /* Draws that used each object's view, in this frame and the last. */
    uint32_t drawn[CAMERA_COUNT], drawn_before[CAMERA_COUNT];
    /* The fallback build of a frozen frame (frame_started) is under way. */
    bool fallback;
    /* C or X picked the object flown. */
    bool picked;
    uint32_t stepped_frame;
    double stepped_seconds;
    RecompInputGamepad pad;
    RecompFreeCamera camera;
    uint32_t trace_lines;
    /* The camera settings list (Tab or BACK alone). */
    bool settings_open, settings_key_held, back_held, back_combo, settings_ready;
    /* The chosen row (-1 on a page's name), its page and its line there. */
    int setting_row;
    unsigned setting_page;
    int setting_line;
    bool reset_row_held, put_back_held;
    /* The field of view the camera started with, for a reset. */
    float start_fov;
    HeldRepeat row_repeat, value_repeat;
    RecompCameraSettings settings;
    /* How far ahead of the camera the chosen character is; zero for none. */
    float character_focus;
    /* Orbit and turntable (camera settings): the sphere the camera is on. */
    RecompFreeCameraOrbit orbit;
    bool orbiting;
    /* Characters the settings hid. */
    uint32_t settings_hidden;
    /* Camera spots: saved views, and which keys were down last sample. */
    RecompFreeCamera spots[RECOMP_CAMERA_SPOTS];
    bool spot_saved[RECOMP_CAMERA_SPOTS];
    unsigned spot_keys;
    bool spot_go_held, spot_save_held;
    /* The camera path and its playback, in frames since it started. */
    RecompCameraPath path;
    bool playing;
    unsigned play_frames;
    /* A playing path's look at this frame, blended from its views'. */
    bool play_looked;
    RecompCameraEffects play_look;
    /* Each girl's own Eyes and Mouth choices (Faces: each girl her own). */
    unsigned girl_eyes[4], girl_mouth[4];
    /* Tracking her (the Track her row): the point followed, smoothed, and
       for following, the camera's place from it. */
    bool tracking, following;
    float track_point[3], track_offset[3];
    /* Hand-held sway: its clock, the frame it last stepped, and the roll it
       adds to the view this frame. */
    double sway_seconds;
    uint32_t sway_frame;
    float sway_roll;
    /* Pictures drawn for each frame shown: more than one with motion blur. */
    unsigned blur;
    /* Slow motion froze the scene, and frames shown since its last step. */
    bool slowed;
    /* P in slow motion holds it where it is; P again goes on slowly. */
    bool slow_paused;
    unsigned slow_frames;
    /* Smooth slow motion: the scene ran on, so it is sampled again (as it
       went on from the last sample, or not); who was hidden at the sample;
       the frame last run unseen. */
    bool sample_due, sample_continues;
    uint32_t sampled_hidden;
    uint32_t unseen_frame;
    bool back_key_held;
    HeldRepeat pose_repeat;
} state = {.enabled = -1, .controller_hints = -1};

/* The settings, camera spots and path are kept between runs
   (camera_store.h): read when the settings are first needed, and written
   once a second at most, when they changed. */
static RecompCameraStore kept;
static uint64_t kept_checked;

static void store_snapshot(RecompCameraStore *store)
{
    memset(store, 0, sizeof *store);
    store->settings = state.settings;
    store->settings.move_her = false;
    store->settings.record = false;
    memcpy(store->spots, state.spots, sizeof store->spots);
    memcpy(store->spot_saved, state.spot_saved, sizeof store->spot_saved);
    store->path = state.path;
    memcpy(store->poses, recomp_actor_pose_library(), sizeof store->poses);
}

static void settings_begin(void)
{
    recomp_camera_settings_default(&state.settings);
    /* Static: the store is large (the pose library), and game fibers' stacks are not. */
    static RecompCameraStore store;
    store_snapshot(&store);
    if (recomp_camera_store_load(recomp_disc_root_path, &store)) {
        state.settings = store.settings;
        memcpy(state.spots, store.spots, sizeof state.spots);
        memcpy(state.spot_saved, store.spot_saved, sizeof state.spot_saved);
        state.path = store.path;
        recomp_actor_pose_set_library(store.poses);
        fprintf(stderr, "recomp free camera: settings, %u spots and a %u-view path kept from the last run\n",
            (unsigned)(store.spot_saved[0] + store.spot_saved[1] + store.spot_saved[2] + store.spot_saved[3]),
            store.path.count);
    }
    store_snapshot(&kept);
    state.settings_ready = true;
}

static uint64_t milliseconds(void);

static void keep_settings(void)
{
    const uint64_t now = milliseconds();
    if (!state.settings_ready || now - kept_checked < 1000u) return;
    kept_checked = now;
    static RecompCameraStore store;
    store_snapshot(&store);
    if (memcmp(&store, &kept, sizeof store) == 0) return;
    if (recomp_camera_store_save(recomp_disc_root_path, &store)) kept = store;
}

static uint64_t milliseconds(void)
{
    struct timespec now;
    if (timespec_get(&now, TIME_UTC) == 0) return 0u;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static double seconds_now(void)
{
    struct timespec now;
    return timespec_get(&now, TIME_UTC) == 0 ? 0.0 : (double)now.tv_sec + now.tv_nsec / 1e9;
}

/* How many places to move now (signed), given the direction held (-1, 0 or
   1). *released is set when the keys were just let go. */
static int held_repeat(HeldRepeat *held, int direction, uint64_t now, bool *released)
{
    *released = false;
    if (direction != 0 && direction != held->direction) {
        held->direction = direction;
        held->since = now;
        held->next = now + CLIP_REPEAT_DELAY_MS;
        return direction;
    }
    if (direction != 0 && now >= held->next) {
        uint64_t time = now - held->since;
        int places = time >= CLIP_FASTEST_AFTER_MS ? 10 : time >= CLIP_FASTER_AFTER_MS ? 3 : 1;
        held->next = now + CLIP_REPEAT_MS;
        return direction * places;
    }
    if (direction == 0 && held->direction != 0) {
        held->direction = 0;
        *released = true;
    }
    return 0;
}

static const char *setting(const char *name)
{
    const char *value = getenv(name);
    return value != NULL && value[0] != '\0' ? value : NULL;
}

static bool enabled(void)
{
    if (state.enabled < 0) {
        const char *value = setting("RECOMP_FREE_CAMERA");
        state.enabled = value != NULL && strcmp(value, "0") != 0;
        const char *slot = setting("RECOMP_FREE_CAMERA_SLOT");
        state.slot = slot != NULL ? (uint32_t)strtoul(slot, NULL, 10) : 0u;
        if (state.slot >= CAMERA_COUNT) state.slot = 0u;
    }
    return state.enabled != 0;
}

/* RECOMP_FREE_CAMERA_TRACE=<frame> logs camera builds from that frame on;
   =on logs them each time the free camera is turned on. */
static bool tracing(uint32_t frame)
{
    const char *from = setting("RECOMP_FREE_CAMERA_TRACE");
    if (from == NULL || state.trace_lines >= TRACE_LINES) return false;
    if (strcmp(from, "on") == 0) return state.active;
    return frame >= (uint32_t)strtoul(from, NULL, 10);
}

/* True when texture replacement keeps plain F12 for itself (screenshot.h).
   Only the Windows presenter has it. */
static bool texture_keys(void)
{
#ifdef _WIN32
    static int on = -1;
    if (on < 0) on = recomp_texture_keys_enabled(getenv("RECOMP_TEXTURE_DUMP"), getenv("RECOMP_TEXTURES"));
    return on != 0;
#else
    return false;
#endif
}

static void set_active(bool active)
{
    if (state.active == active) return;
    state.active = active;
    state.started = false;
    state.trace_lines = 0u;
    if (!active) {
        state.home_saved = false;
        state.picked = false;
        state.settings_open = false;
        state.playing = false;
        /* Putting the camera away hands the scene back, running. */
        recomp_actor_pose_release();
        recomp_actor_pose_place_reset(true);
        recomp_suit_switch_forget();
        recomp_scene_freeze_set(false);
        recomp_scene_freeze_set_overlay_hidden(false);
        state.panel_hidden = false;
    }
    fprintf(stderr, "recomp free camera: %s\n", active ? "on" : "off");
}

static void sample_while_frozen(uint32_t frame);
static bool intercept_screenshot(void);
static void frame_started(void);
static void draw_viewed(const float view[16]);
static void scene_runs(void);
static void step_tracked(const RecompFreeCameraControls *controls, float seconds);
static void face_swap(RecompCameraSettingRow row);
static void describe_pose_row(RecompCameraSettingRow row, char *text, size_t size);
static void apply_studio_lights(void);

void recomp_free_camera_set_source(RecompInputSampleSource source)
{
    state.source = source;
    /* Off, the free camera leaves the game alone: only the input passes
       through it. Skin shading still needs the girls' draws marked. */
    if (!enabled()) {
        if (recomp_camera_effects_skin_wanted())
            recomp_d3d_draw_set_character_test(recomp_actor_pose_character_world);
        return;
    }
    recomp_scene_freeze_set_frame_hook(sample_while_frozen);
    recomp_scene_freeze_set_run_hook(scene_runs);
    recomp_d3d_draw_set_character_test(recomp_actor_pose_character_world);
    recomp_d3d_frame_adapter_set_first_clear_hook(frame_started);
    recomp_d3d_draw_set_view_observer(draw_viewed);
    recomp_screenshot_set_interceptor(intercept_screenshot);
}

/* Handles the toggles in one sample of port 0. True when the sample is the
   free camera's rather than the game's. */
static float stick(int16_t value);

static bool controller_used(const RecompInputGamepad *gamepad)
{
    if (gamepad->buttons != 0u) return true;
    for (unsigned i = 0u; i < sizeof gamepad->analog_buttons; ++i) {
        if (gamepad->analog_buttons[i] >= ANALOG_PRESSED) return true;
    }
    return stick(gamepad->thumb_lx) != 0.0f || stick(gamepad->thumb_ly) != 0.0f ||
        stick(gamepad->thumb_rx) != 0.0f || stick(gamepad->thumb_ry) != 0.0f;
}

static bool keys_used(void)
{
    for (int key = 0; key < RECOMP_INPUT_HOST_KEY_COUNT; ++key) {
        if (recomp_input_host_key((RecompInputHostKey)key)) return true;
    }
    return false;
}

/* The input host also presses controller buttons for some keys, for the
   game (input_host_win32.c). The free camera reads those keys itself, so
   their buttons are left out of what it reads: holding S would otherwise
   press Y and reset the view, A would press X and change the camera object,
   W and Q would toggle the clean view and step, and the arrows would press
   the d-pad and change the animation and swimsuit. */
static void leave_out_keys(RecompInputGamepad *pad)
{
    static const struct {
        RecompInputHostKey key;
        unsigned analog;
    } analog_keys[] = {
        {RECOMP_INPUT_HOST_KEY_A, ANALOG_X}, {RECOMP_INPUT_HOST_KEY_S, ANALOG_Y},
        {RECOMP_INPUT_HOST_KEY_Z, ANALOG_A}, {RECOMP_INPUT_HOST_KEY_X, ANALOG_B},
        {RECOMP_INPUT_HOST_KEY_Q, ANALOG_WHITE}, {RECOMP_INPUT_HOST_KEY_W, ANALOG_BLACK},
        {RECOMP_INPUT_HOST_KEY_E, ANALOG_LTRIG}, {RECOMP_INPUT_HOST_KEY_R, ANALOG_RTRIG},
    };
    static const struct {
        RecompInputHostKey key;
        uint16_t button;
    } button_keys[] = {
        {RECOMP_INPUT_HOST_KEY_UP, XBOX_DPAD_UP}, {RECOMP_INPUT_HOST_KEY_DOWN, XBOX_DPAD_DOWN},
        {RECOMP_INPUT_HOST_KEY_LEFT, XBOX_DPAD_LEFT}, {RECOMP_INPUT_HOST_KEY_RIGHT, XBOX_DPAD_RIGHT},
    };
    for (size_t i = 0u; i < sizeof analog_keys / sizeof analog_keys[0]; ++i) {
        if (recomp_input_host_key(analog_keys[i].key)) pad->analog_buttons[analog_keys[i].analog] = 0u;
    }
    for (size_t i = 0u; i < sizeof button_keys / sizeof button_keys[0]; ++i) {
        if (recomp_input_host_key(button_keys[i].key)) pad->buttons &= (uint16_t)~button_keys[i].button;
    }
}

static bool any_other_button(const RecompInputGamepad *gamepad)
{
    if ((gamepad->buttons & (uint16_t)~XBOX_BACK) != 0u) return true;
    for (unsigned i = 0u; i < sizeof gamepad->analog_buttons; ++i) {
        if (gamepad->analog_buttons[i] >= ANALOG_PRESSED) return true;
    }
    return false;
}

/* Back (step < 0) or forward through the poses of the last frames; forward
   from the newest runs the scene one frame. Freezes the scene first. */
static void step_pose(int step)
{
    if (!recomp_scene_freeze_active()) recomp_scene_freeze_set(true);
    if (step < 0 || recomp_actor_pose_history_back() > 0u) recomp_actor_pose_history_step(step);
    else recomp_scene_freeze_step();
}

/* Saves the flown view in a spot, or goes to a saved one (for this run). */
static void use_spot(unsigned spot, bool save)
{
    if (spot >= RECOMP_CAMERA_SPOTS || !state.started) return;
    if (save) {
        state.spots[spot] = state.camera;
        state.spot_saved[spot] = true;
        fprintf(stderr, "recomp free camera: saved spot %u\n", spot + 1u);
    } else if (state.spot_saved[spot]) {
        state.camera = state.spots[spot];
        state.orbiting = false;
    }
}

/* Every camera setting back as it starts, the field of view too. Camera
   spots and the path are views, not settings: they stay. */
static void reset_settings(void)
{
    recomp_camera_settings_default(&state.settings);
    memset(state.girl_eyes, 0, sizeof state.girl_eyes);
    memset(state.girl_mouth, 0, sizeof state.girl_mouth);
    if (state.started && state.start_fov > 0.0f) state.camera.fov = state.start_fov;
    recomp_actor_pose_place_reset(true);
    fprintf(stderr, "recomp free camera: settings reset\n");
}

/* Tab, or BACK let go without another button (BACK with A is a screenshot),
   opens and closes the settings; while open, up and down pick a row and
   left and right change it, held to keep going. */
static void take_settings_input(const RecompInputGamepad *gamepad, uint64_t now)
{
    if (!state.settings_ready) settings_begin();
    bool tab = state.active && recomp_input_host_key(RECOMP_INPUT_HOST_KEY_TAB);
    if (tab && !state.settings_key_held) state.settings_open = !state.settings_open;
    state.settings_key_held = tab;
    bool back = state.active && (gamepad->buttons & XBOX_BACK) != 0u;
    if (back) {
        if (!state.back_held) state.back_combo = false;
        if (any_other_button(gamepad)) state.back_combo = true;
    } else if (state.back_held && !state.back_combo && state.active) {
        state.settings_open = !state.settings_open;
    }
    state.back_held = back;
    if (!state.active) state.settings_open = false;
    if (!state.settings_open) return;

    bool released;
    bool up = recomp_input_host_key(RECOMP_INPUT_HOST_KEY_UP) || (gamepad->buttons & XBOX_DPAD_UP) != 0u;
    bool down = recomp_input_host_key(RECOMP_INPUT_HOST_KEY_DOWN) || (gamepad->buttons & XBOX_DPAD_DOWN) != 0u;
    bool left = recomp_input_host_key(RECOMP_INPUT_HOST_KEY_LEFT) || (gamepad->buttons & XBOX_DPAD_LEFT) != 0u;
    bool right = recomp_input_host_key(RECOMP_INPUT_HOST_KEY_RIGHT) || (gamepad->buttons & XBOX_DPAD_RIGHT) != 0u;
    /* The list is in pages: a page's first line names it, and left and
       right there turn the page; the rows below it are that page's. */
    const RecompCameraSettingsPage *page = recomp_camera_settings_page(state.setting_page);
    const int lines = (int)page->count + 1;
    int rows = held_repeat(&state.row_repeat, (int)down - (int)up, now, &released);
    if (rows != 0) {
        /* Rows move one at a time however long the key is held. */
        const int line = state.setting_line + (rows > 0 ? 1 : -1);
        state.setting_line = (line % lines + lines) % lines;
    }
    int steps = held_repeat(&state.value_repeat, (int)right - (int)left, now, &released);
    if (steps != 0 && state.setting_line == 0) {
        const int pages = (int)recomp_camera_settings_page_count();
        state.setting_page = (unsigned)((((int)state.setting_page + (steps > 0 ? 1 : -1)) % pages + pages) % pages);
        steps = 0;
    }
    page = recomp_camera_settings_page(state.setting_page);
    if (state.setting_line > (int)page->count) state.setting_line = (int)page->count;
    state.setting_row = state.setting_line == 0 ? -1 : (int)page->rows[state.setting_line - 1];
    if (steps != 0 && state.setting_row == RECOMP_CAMERA_SETTING_PATH) {
        if (steps > 0 && state.started) {
            /* The view keeps its look: the settings' effects as they are now. */
            RecompCameraEffects look;
            recomp_camera_settings_effects(&state.settings, state.camera.fov, state.character_focus, &look);
            recomp_camera_path_add_look(&state.path, &state.camera, &look);
        }
        else if (steps < 0) recomp_camera_path_remove(&state.path);
    } else if (steps != 0 && state.setting_row == RECOMP_CAMERA_SETTING_PATH_PLAY) {
        if (steps > 0 && state.path.count >= 2u) {
            state.playing = true;
            state.play_frames = 0u;
            state.orbiting = false;
        } else if (steps < 0) {
            state.playing = false;
        }
    } else if (steps > 0 && state.setting_row == RECOMP_CAMERA_SETTING_POSE_KEEP) {
        if (state.started && recomp_actor_pose_keep_pose(state.settings.pose_slot))
            fprintf(stderr, "recomp free camera: kept her pose in slot %u\n", state.settings.pose_slot + 1u);
    } else if (steps != 0 && state.setting_row == RECOMP_CAMERA_SETTING_POSE_HOLD) {
        const bool holding = recomp_actor_pose_held_pose() == (int)state.settings.pose_slot;
        recomp_actor_pose_hold_pose(state.settings.pose_slot, !holding);
    } else if (steps != 0 && state.setting_row == RECOMP_CAMERA_SETTING_RESET) {
        reset_settings();
    } else if (steps != 0 && state.setting_row == RECOMP_CAMERA_SETTING_POSE) {
        step_pose(steps > 0 ? 1 : -1);
    } else if (steps != 0 && state.setting_row >= 0) {
        face_swap((RecompCameraSettingRow)state.setting_row);
        recomp_camera_settings_change(&state.settings, (RecompCameraSettingRow)state.setting_row,
            steps, &state.camera.fov, state.character_focus);
        face_swap((RecompCameraSettingRow)state.setting_row);
    }
    const bool put_back = state.setting_row == RECOMP_CAMERA_SETTING_MOVE_HER &&
        gamepad->analog_buttons[ANALOG_A] >= ANALOG_PRESSED;
    if (put_back && !state.put_back_held) recomp_actor_pose_place_reset(false);
    state.put_back_held = put_back;
    const bool reset = state.setting_row == RECOMP_CAMERA_SETTING_RESET &&
        gamepad->analog_buttons[ANALOG_A] >= ANALOG_PRESSED;
    if (reset && !state.reset_row_held) reset_settings();
    state.reset_row_held = reset;
    /* On the Camera spot row, A goes to the spot and X saves the view there. */
    const bool spot_row = state.setting_row == RECOMP_CAMERA_SETTING_SPOT;
    const bool go = spot_row && gamepad->analog_buttons[ANALOG_A] >= ANALOG_PRESSED;
    const bool save = spot_row && gamepad->analog_buttons[ANALOG_X] >= ANALOG_PRESSED;
    if (go && !state.spot_go_held) use_spot(state.settings.spot, false);
    if (save && !state.spot_save_held) use_spot(state.settings.spot, true);
    state.spot_go_held = go;
    state.spot_save_held = save;
}

/* Keys 1 to 4 go to a camera spot, with Shift they save the view there. */
static void take_spot_keys(void)
{
    static const RecompInputHostKey keys[RECOMP_CAMERA_SPOTS] = {RECOMP_INPUT_HOST_KEY_1,
        RECOMP_INPUT_HOST_KEY_2, RECOMP_INPUT_HOST_KEY_3, RECOMP_INPUT_HOST_KEY_4};
    unsigned down = 0u;
    for (unsigned i = 0u; i < RECOMP_CAMERA_SPOTS; ++i) {
        if (!state.active || !recomp_input_host_key(keys[i])) continue;
        down |= 1u << i;
        if ((state.spot_keys & (1u << i)) == 0u) use_spot(i, recomp_input_host_key(RECOMP_INPUT_HOST_KEY_SHIFT));
    }
    state.spot_keys = down;
}

/* Every screenshot request (keys, controller, trigger file) comes here
   first: while flying with a size above 1x the presenter draws the next
   frame that many times larger and saves it instead. */
static bool intercept_screenshot(void)
{
    if (!state.active || !state.started || !state.settings_ready || state.settings.shot_size < 2u) return false;
    recomp_camera_effects_request_big_shot(state.settings.shot_size);
    return true;
}

static bool take_sample(const RecompInputGamepad *raw)
{
    RecompInputGamepad pad = *raw;
    leave_out_keys(&pad);
    const RecompInputGamepad *gamepad = &pad;
    if (state.controller_hints < 0) {
        const char *deck = getenv("SteamDeck");
        state.controller_hints = deck != NULL && strcmp(deck, "1") == 0;
    }
    if (keys_used()) state.controller_hints = 0;
    else if (controller_used(gamepad)) state.controller_hints = 1;

    bool toggle = recomp_input_host_key(RECOMP_INPUT_HOST_KEY_F1) ||
        (gamepad->buttons & XBOX_RIGHT_THUMB) != 0u;
    if (toggle && !state.toggle_held) set_active(!state.active);
    state.toggle_held = toggle;

    /* F12, or Shift+F12 beside texture replacement, saves a screenshot
       anywhere; Back with A while flying. */
    bool shot = recomp_screenshot_key(recomp_input_host_key(RECOMP_INPUT_HOST_KEY_F12),
                    recomp_input_host_key(RECOMP_INPUT_HOST_KEY_SHIFT), texture_keys()) ||
        (state.active && (gamepad->buttons & XBOX_BACK) != 0u &&
         gamepad->analog_buttons[ANALOG_A] >= ANALOG_PRESSED);
    if (shot && !state.shot_held) recomp_screenshot_request();
    state.shot_held = shot;

    /* P pauses anywhere, flying or not; Start only while flying, since the
       game has its own use for it. The game's keyboard bindings leave P, H
       and . free. */
    bool freeze = recomp_input_host_key(RECOMP_INPUT_HOST_KEY_P) ||
        (state.active && (gamepad->buttons & XBOX_START) != 0u);
    if (freeze && !state.freeze_held && state.slowed && recomp_scene_freeze_active()) {
        state.slow_paused = !state.slow_paused;
        fprintf(stderr, "recomp free camera: slow motion %s\n", state.slow_paused ? "paused" : "goes on");
    } else if (freeze && !state.freeze_held) {
        bool pause = !recomp_scene_freeze_active();
        recomp_scene_freeze_set(pause);
        /* A trace starts again at each pause. */
        state.trace_lines = 0u;
        if (!pause && !state.active) {
            recomp_scene_freeze_set_overlay_hidden(false);
            state.panel_hidden = false;
        }
    }
    state.freeze_held = freeze;
    const bool paused = recomp_scene_freeze_active();

    bool overlay = (state.active || paused) && (recomp_input_host_key(RECOMP_INPUT_HOST_KEY_H) ||
        (state.active && gamepad->analog_buttons[ANALOG_BLACK] >= ANALOG_PRESSED));
    if (overlay && !state.overlay_held) {
        /* A clean view hides the game's overlay and the runtime's panel
           together; the next press shows both. The model viewer keeps the
           game's overlay hidden throughout, so there it shows the panel. */
        bool hide = !state.panel_hidden;
        state.panel_hidden = hide;
        recomp_scene_freeze_set_overlay_hidden(hide);
    }
    state.overlay_held = overlay;

    bool step = (state.active || paused) && (recomp_input_host_key(RECOMP_INPUT_HOST_KEY_PERIOD) ||
        (state.active && gamepad->analog_buttons[ANALOG_WHITE] >= ANALOG_PRESSED));
    /* Holding it steps on and on, every frame after a while. */
    bool step_released;
    int steps = held_repeat(&state.step_repeat, step ? 1 : 0, milliseconds(), &step_released);
    if (steps != 0 || (step && milliseconds() - state.step_repeat.since >= CLIP_FASTER_AFTER_MS)) {
        if (recomp_actor_pose_history_back() > 0u) recomp_actor_pose_history_step(1);
        else recomp_scene_freeze_step();
    }
    /* , steps the poses back while frozen; held, it keeps going. */
    bool pose_back = (state.active || paused) && recomp_input_host_key(RECOMP_INPUT_HOST_KEY_COMMA);
    bool pose_released;
    if (held_repeat(&state.pose_repeat, pose_back ? -1 : 0, milliseconds(), &pose_released) != 0) step_pose(-1);

    uint64_t now = milliseconds();
    bool released;
    take_settings_input(gamepad, now);
    take_spot_keys();
    /* With the settings open the d-pad is theirs. */
    const uint16_t dpad = state.settings_open ? 0u : gamepad->buttons;

    /* A press moves to the next or previous clip; holding keeps moving,
       faster the longer it is held, and the clip plays when let go. */
    bool clip_back = state.active && (recomp_input_host_key(RECOMP_INPUT_HOST_KEY_LEFT_BRACKET) ||
        (dpad & XBOX_DPAD_LEFT) != 0u);
    bool clip_next = state.active && (recomp_input_host_key(RECOMP_INPUT_HOST_KEY_RIGHT_BRACKET) ||
        (dpad & XBOX_DPAD_RIGHT) != 0u);
    int places = held_repeat(&state.clip_held, (int)clip_next - (int)clip_back, now, &released);
    if (places != 0) recomp_actor_pose_browse(places);
    if (released && recomp_actor_pose_play_browsed() && recomp_scene_freeze_active()) {
        recomp_scene_freeze_step();
    }

    /* The model viewer shows one girl, so it has no next one. */
    bool actor = state.active && !state.viewer && (recomp_input_host_key(RECOMP_INPUT_HOST_KEY_BACKSLASH) ||
        (gamepad->buttons & XBOX_LEFT_THUMB) != 0u);
    if (actor && !state.actor_held) {
        recomp_actor_pose_next_actor();
        state.aim_pending = true;
    }
    state.actor_held = actor;

    /* Swimsuits move the same way and the suit loads when let go. */
    bool suit_back = state.active && (recomp_input_host_key(RECOMP_INPUT_HOST_KEY_MINUS) ||
        (dpad & XBOX_DPAD_DOWN) != 0u);
    bool suit_next = state.active && (recomp_input_host_key(RECOMP_INPUT_HOST_KEY_EQUALS) ||
        (dpad & XBOX_DPAD_UP) != 0u);
    places = held_repeat(&state.suit_held, (int)suit_next - (int)suit_back, now, &released);
    if (places != 0) {
        RecompActorPoseStatus pose;
        recomp_actor_pose_status(&pose);
        recomp_suit_switch_browse(pose.actor, places);
    }
    if (released) recomp_suit_switch_wear_browsed();

    if (state.active || toggle) state.pad = *gamepad;
    return state.active || toggle;
}

bool recomp_free_camera_sample(uint32_t port, RecompInputGamepad *gamepad)
{
    bool present = false;

    if (gamepad == NULL) return false;
    memset(gamepad, 0, sizeof *gamepad);
    if (state.source != NULL) present = state.source(port, gamepad);
    if (port != 0u || !enabled()) return present;
    if (take_sample(gamepad)) memset(gamepad, 0, sizeof *gamepad);
    else recomp_viewer_input(gamepad);
    return present;
}

/* While frozen the game stops polling; the freeze calls this once a frame. */
static void sample_while_frozen(uint32_t frame)
{
    (void)frame;
    RecompInputGamepad gamepad;
    memset(&gamepad, 0, sizeof gamepad);
    if (state.source != NULL) state.source(0u, &gamepad);
    take_sample(&gamepad);
}

void sub_00023B10(void);

static float stick(int16_t value)
{
    float axis = (float)value / 32767.0f;
    if (axis > 1.0f) axis = 1.0f;
    if (axis < -1.0f) axis = -1.0f;
    if (axis > -STICK_DEADZONE && axis < STICK_DEADZONE) return 0.0f;
    return axis;
}

static float key(RecompInputHostKey positive, RecompInputHostKey negative)
{
    return (recomp_input_host_key(positive) ? 1.0f : 0.0f) -
        (recomp_input_host_key(negative) ? 1.0f : 0.0f);
}

static bool analog(const RecompInputGamepad *pad, unsigned index)
{
    return pad->analog_buttons[index] >= ANALOG_PRESSED;
}

static void read_controls(RecompFreeCameraControls *controls)
{
    const RecompInputGamepad *pad = &state.pad;

    memset(controls, 0, sizeof *controls);
    controls->move[0] = stick(pad->thumb_lx) + key(RECOMP_INPUT_HOST_KEY_D, RECOMP_INPUT_HOST_KEY_A);
    controls->move[1] = ((float)pad->analog_buttons[ANALOG_RTRIG] -
        (float)pad->analog_buttons[ANALOG_LTRIG]) / 255.0f +
        key(RECOMP_INPUT_HOST_KEY_E, RECOMP_INPUT_HOST_KEY_Q);
    controls->move[2] = stick(pad->thumb_ly) + key(RECOMP_INPUT_HOST_KEY_W, RECOMP_INPUT_HOST_KEY_S);
    /* With the settings open the arrows are theirs; the right stick and the
       mouse still look. */
    const float arrows = state.settings_open ? 0.0f : 1.0f;
    controls->look[0] = stick(pad->thumb_rx) +
        arrows * key(RECOMP_INPUT_HOST_KEY_RIGHT, RECOMP_INPUT_HOST_KEY_LEFT);
    controls->look[1] = stick(pad->thumb_ry) +
        arrows * key(RECOMP_INPUT_HOST_KEY_UP, RECOMP_INPUT_HOST_KEY_DOWN);
    /* The d-pad's up and down change the swimsuit; only keys zoom. */
    controls->zoom = key(RECOMP_INPUT_HOST_KEY_Z, RECOMP_INPUT_HOST_KEY_X);
    /* Dragging with the right mouse button aims; a narrow view aims finer. */
    float drag_x, drag_y;
    if (recomp_input_host_mouse_drag(&drag_x, &drag_y)) {
        controls->turn[0] = drag_x * MOUSE_RADIANS_PER_PIXEL * state.camera.fov;
        controls->turn[1] = -drag_y * MOUSE_RADIANS_PER_PIXEL * state.camera.fov;
    }
    controls->fast = analog(pad, ANALOG_A) || recomp_input_host_key(RECOMP_INPUT_HOST_KEY_SHIFT);
    controls->slow = analog(pad, ANALOG_B) || recomp_input_host_key(RECOMP_INPUT_HOST_KEY_CONTROL);

    /* Debug: RECOMP_FREE_CAMERA_DRIFT=right,up,forward,turn,pitch[,frame]
       adds a constant input, from that frame on if given, so a scripted run
       can move the camera. */
    const char *drift = setting("RECOMP_FREE_CAMERA_DRIFT");
    float add[5] = {0};
    unsigned from = 0u;
    if (drift != NULL && sscanf(drift, "%f,%f,%f,%f,%f,%u",
        &add[0], &add[1], &add[2], &add[3], &add[4], &from) >= 5 &&
        recomp_d3d_frame_adapter_swap_counter() >= from) {
        for (unsigned i = 0u; i < 3u; ++i) controls->move[i] += add[i];
        controls->look[0] += add[3];
        controls->look[1] += add[4];
    }
}

/* Reset also hands the chosen character's animation back to the game and
   puts her first swimsuit back on. */
static void reset_character(void)
{
    RecompActorPoseStatus pose;
    recomp_actor_pose_status(&pose);
    bool posed = pose.holding;
    recomp_actor_pose_release();
    recomp_suit_switch_restore(pose.actor);
    if (posed && recomp_scene_freeze_active()) recomp_scene_freeze_step();
}

/* Counts a draw against the objects whose view it used. */
static void draw_viewed(const float view[16])
{
    for (uint32_t slot = 0u; slot < CAMERA_COUNT; ++slot) {
        float matrix[16];
        recomp_guest_load(matrix, CAMERA_FIRST + slot * CAMERA_STRIDE + CAMERA_VIEW, sizeof matrix);
        bool same = true;
        for (unsigned i = 0u; i < 16u && same; ++i) {
            float difference = matrix[i] - view[i];
            float size = fabsf(matrix[i]) > 1.0f ? fabsf(matrix[i]) : 1.0f;
            same = fabsf(difference) <= 1e-4f * size;
        }
        if (same) ++state.drawn[slot];
    }
}

/* Flies the object the scene is drawn with: on the way in, and whenever
   the one flown stops being built. */
static void choose_object(void)
{
    if (state.built_before == 0u) return;
    if ((state.started || state.picked) && (state.built_before & (1u << state.slot)) != 0u) return;
    state.picked = false;
    uint32_t best = state.slot;
    for (uint32_t slot = 0u; slot < CAMERA_COUNT; ++slot) {
        if ((state.built_before & (1u << slot)) == 0u) continue;
        if ((state.built_before & (1u << best)) == 0u ||
            state.drawn_before[slot] > state.drawn_before[best]) best = slot;
    }
    state.slot = best;
    state.started = false;
    fprintf(stderr, "recomp free camera: object %u (built %02x, draws %u %u %u %u %u)\n", best,
        state.built_before, state.drawn_before[0], state.drawn_before[1], state.drawn_before[2],
        state.drawn_before[3], state.drawn_before[4]);
}

/* "Focus on her": the presenter finds the character nearest the middle of
   the picture (camera_effects.h); the list shows her distance. */
static void update_character_focus(void)
{
    state.character_focus = recomp_camera_effects_subject();
}

/* Row-vector product a * b. */
static void multiply(const float a[16], const float b[16], float out[16])
{
    for (unsigned r = 0u; r < 4u; ++r)
        for (unsigned c = 0u; c < 4u; ++c) {
            float sum = 0.0f;
            for (unsigned k = 0u; k < 4u; ++k) sum += a[r * 4u + k] * b[k * 4u + c];
            out[r * 4u + c] = sum;
        }
}

/* Hand-held: the view sways a little, as a camera held in the hands does,
   from sums of slow sines at unrelated rates (so the motion never visibly
   repeats): heading, pitch and roll up to SWAY_ANGLE times the strength,
   and the camera's place by a centimetre or two. Its clock steps a frame's
   time each frame (a motion blur's picture's share of one), so a recorded
   video sways the same however fast it was drawn. */
static void sway(RecompFreeCamera *view, uint32_t frame)
{
    state.sway_roll = 0.0f;
    const unsigned strength = state.settings_ready ? state.settings.handheld : 0u;
    if (strength == 0u) return;
    if (frame != state.sway_frame) {
        state.sway_frame = frame;
        state.sway_seconds += FRAME_SECONDS / (double)(state.blur ? state.blur : 1u);
    }
    const double t = state.sway_seconds;
    const float amount = (float)strength * (float)strength * 0.5f + (float)strength * 0.5f; /* 1, 3, 6 */
    const float angle = amount * SWAY_ANGLE;
    view->heading += angle * (float)(0.6 * sin(t * 0.83) + 0.3 * sin(t * 2.11 + 1.3) + 0.1 * sin(t * 5.3 + 0.4));
    view->pitch += angle * 0.8f * (float)(0.6 * sin(t * 0.67 + 2.0) + 0.3 * sin(t * 1.93 + 0.7) + 0.1 * sin(t * 4.7 + 2.2));
    state.sway_roll = angle * 0.5f * (float)(0.7 * sin(t * 0.51 + 0.9) + 0.3 * sin(t * 1.37 + 2.6));
    const float drift = amount * 0.004f;
    view->position[0] += drift * (float)sin(t * 0.43 + 1.1);
    view->position[1] += drift * (float)sin(t * 0.71 + 0.2);
    view->position[2] += drift * (float)sin(t * 0.59 + 2.4);
}

/* After the game builds the flown object: the settings' roll turns its view
   about the line of sight. The game culls by the eye and target, which a
   roll only narrows. */
static void turn_view(uint32_t object)
{
    const float roll = (state.playing && state.play_looked ? state.play_look.roll :
        state.settings_ready ? state.settings.roll * 3.14159265f / 180.0f : 0.0f) + state.sway_roll;
    if (roll == 0.0f) return;
    const float c = cosf(roll), s = sinf(roll);
    const float turn[16] = {c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float view[16], turned[16];
    recomp_guest_load(view, object + CAMERA_VIEW, sizeof view);
    multiply(view, turn, turned);
    recomp_guest_store(object + CAMERA_VIEW, turned, sizeof turned);
}

static void build_camera(void)
{
    const uint32_t object = recomp_runtime.registers.eax;
    const uint32_t frame = recomp_d3d_frame_adapter_swap_counter();
    const uint32_t slot = (object - CAMERA_FIRST) / CAMERA_STRIDE;
    const bool known = object >= CAMERA_FIRST && slot < CAMERA_COUNT &&
        object == CAMERA_FIRST + slot * CAMERA_STRIDE;

    if (!state.fallback) state.game_built_frame = frame;
    if (state.built_frame != frame) {
        recomp_actor_pose_frame();
        state.built_before = state.built;
        state.built = 0u;
        memcpy(state.drawn_before, state.drawn, sizeof state.drawn);
        memset(state.drawn, 0, sizeof state.drawn);
        state.built_frame = frame;
        if (state.active) choose_object();
    }
    if (known) state.built |= 1u << slot;
    recomp_suit_switch_camera_build();
    if (state.active) {
        /* With the settings open, X saves a camera spot instead. */
        bool cycle = !state.settings_open &&
            (analog(&state.pad, ANALOG_X) || recomp_input_host_key(RECOMP_INPUT_HOST_KEY_C));
        if (cycle && !state.cycle_held) {
            for (uint32_t step = 1u; step <= CAMERA_COUNT; ++step) {
                uint32_t next = (state.slot + step) % CAMERA_COUNT;
                if ((state.built_before & (1u << next)) != 0u) {
                    if (next != state.slot) {
                        state.slot = next;
                        state.picked = true;
                        state.started = false;
                        fprintf(stderr, "recomp free camera: object %u\n", next);
                    }
                    break;
                }
            }
        }
        state.cycle_held = cycle;
    }
    const bool ours = known && slot == state.slot;
    float eye[3], target[3], fov;

    recomp_guest_load(eye, object + CAMERA_EYE, sizeof eye);
    recomp_guest_load(target, object + CAMERA_TARGET, sizeof target);
    recomp_guest_load(&fov, object + CAMERA_FOV, sizeof fov);
    if (tracing(frame)) {
        ++state.trace_lines;
        /* The game task building it: the scheduler's current record's function. */
        uint32_t task = *recomp_memory_u32(0x005F2E5Cu);
        uint32_t function = task < 17u ? *recomp_memory_u32(0x005F2C38u + task * 0x20u + 0x0Cu) : 0u;
        fprintf(stderr, "recomp free camera trace: frame=%u object=%08x active=%d task=%08x "
            "eye=%.3f,%.3f,%.3f target=%.3f,%.3f,%.3f fov=%.4f\n",
            frame, object, state.active && ours, function, eye[0], eye[1], eye[2],
            target[0], target[1], target[2], fov);
    }

    const bool fly = state.active && ours;
    if (fly) {
        bool reset = analog(&state.pad, ANALOG_Y) || recomp_input_host_key(RECOMP_INPUT_HOST_KEY_R);
        bool resetting = reset && !state.reset_held;
        if (resetting) reset_character();
        if (resetting) state.orbiting = false;
        if (resetting && state.viewer && state.started && state.home_saved) {
            /* The viewer's view is the one it first framed her in; framing
               her again would follow her moving hips. */
            state.camera = state.home;
        } else if ((!state.started || resetting) &&
            recomp_free_camera_begin(&state.camera, eye, target, fov)) {
            state.started = true;
            state.start_fov = state.camera.fov;
            state.stepped_frame = frame;
            state.stepped_seconds = seconds_now();
        }
        state.reset_held = reset;
        if (state.started && state.stepped_frame != frame) {
            RecompFreeCameraControls controls;
            read_controls(&controls);
            /* Above RECOMP_GAME_HZ=60, display passes build the camera between
               ticks, so a step covers the time since the last one; capped so a
               stall does not jump the camera. */
            const double now = seconds_now();
            double seconds = now - state.stepped_seconds;
            if (!(seconds > 0.0) || seconds > 0.1) seconds = FRAME_SECONDS;
            if (state.playing) {
                /* The path plays at 60 frames a second of its length, each
                   frame `blur` pictures with motion blur. */
                const unsigned steps = 60u * state.settings.path_seconds * (state.blur ? state.blur : 1u);
                const float t = (float)state.play_frames / (float)steps;
                recomp_camera_path_sample(&state.path, t, &state.camera);
                state.play_looked = recomp_camera_path_sample_look(&state.path, t, &state.play_look);
                /* A frame run unseen is not one of them. */
                if (frame != state.unseen_frame && ++state.play_frames > steps)
                    state.playing = false;
            } else if (state.settings.orbit != RECOMP_CAMERA_ORBIT_OFF) {
                /* Round the point in focus: the character in the middle of
                   the picture, or the camera's target. */
                if (!state.orbiting) {
                    float distance = state.character_focus > 0.0f ? state.character_focus : state.camera.focus;
                    state.orbiting = recomp_free_camera_orbit_begin(&state.camera, distance, &state.orbit);
                }
                const float spin = state.settings.orbit == RECOMP_CAMERA_ORBIT_TURNTABLE ? TURNTABLE_RATE : 0.0f;
                if (state.orbiting)
                    recomp_free_camera_orbit_step(&state.camera, &state.orbit, &controls, spin, (float)seconds);
            } else if (state.settings.move_her) {
                /* The sticks move and turn her; the camera stays. */
                state.orbiting = false;
                const float scale = controls.fast ? 4.0f : controls.slow ? 0.25f : 1.0f;
                const float step = MOVE_HER_SPEED * scale * (float)seconds;
                recomp_actor_pose_place(controls.move[0] * step, controls.move[2] * step, state.camera.heading,
                    -controls.look[0] * MOVE_HER_TURN * scale * (float)seconds);
            } else {
                state.orbiting = false;
                step_tracked(&controls, (float)seconds);
            }
            state.stepped_frame = frame;
            state.stepped_seconds = now;
        }
    }
    if (fly && state.started && (state.aim_pending || state.frame_pending)) {
        float point[3];
        if (recomp_actor_pose_focus(point)) {
            if (state.frame_pending) {
                float facing[3];
                point[1] += FRAME_LIFT;
                if (recomp_actor_pose_facing(facing)) {
                    /* Stand in front of her: frame from a point ahead of
                       her hips, so the line of sight runs back to them. */
                    for (unsigned i = 0u; i < 3u; ++i) {
                        state.camera.position[i] = point[i] + facing[i] * FRAME_DISTANCE;
                    }
                }
                recomp_free_camera_frame(&state.camera, point, FRAME_DISTANCE, FRAME_FOV);
            } else {
                recomp_free_camera_look_at(&state.camera, point);
            }
        }
        if (state.frame_pending && state.save_home) {
            state.home = state.camera;
            state.home_saved = true;
            state.save_home = false;
        }
        state.aim_pending = false;
        state.frame_pending = false;
    }
    if (fly && state.started) update_character_focus();
    if (fly && state.started) {
        float flown[3];
        // The game is shown the view swayed as if held; the camera itself stays.
        RecompFreeCamera shown = state.camera;
        sway(&shown, frame);
        recomp_free_camera_target(&shown, flown);
        recomp_guest_store(object + CAMERA_EYE, shown.position, sizeof shown.position);
        recomp_guest_store(object + CAMERA_TARGET, flown, sizeof flown);
        recomp_guest_store(object + CAMERA_FOV, &shown.fov, sizeof shown.fov);
    }

    /* The animation probe observes the same build when its traces run. */
    RecompFunction observed = recomp_animation_probe_lookup_manual(BUILD_CAMERA);
    if (observed != NULL) observed();
    else sub_00023B10();
    if (fly && state.started) turn_view(object);

}

/* The pose library's rows: the slot and whose pose it keeps, keeping hers,
   and holding it on her. */
static void describe_pose_row(RecompCameraSettingRow row, char *text, size_t size)
{
    const unsigned slot = state.settings.pose_slot;
    const RecompPoseSlot *kept = recomp_actor_pose_library() + slot;
    const char *whose = kept->used ? recomp_camera_settings_girl_name(kept->type) : NULL;
    if (row == RECOMP_CAMERA_SETTING_POSE_SLOT) {
        snprintf(text, size, "Saved pose: slot %u, %s", slot + 1u,
            !kept->used ? "empty" : whose != NULL ? whose : "kept");
    } else if (row == RECOMP_CAMERA_SETTING_POSE_KEEP) {
        snprintf(text, size, "Keep her pose in slot %u: press right", slot + 1u);
    } else {
        const bool holding = recomp_actor_pose_held_pose() == (int)slot;
        const bool fits = kept->used && kept->type == recomp_actor_pose_chosen_type();
        snprintf(text, size, "Hold the saved pose on her: %s", holding ? (fits ? "On" : "On (not her pose)")
            : !kept->used ? "Off (slot empty)" : fits ? "Off" : "Off (another girl's pose)");
    }
}

/* With Faces each girl's own, the Eyes and Mouth rows show and change the
   chosen girl's choices: they are swapped into the settings around the
   row's change or description, and back. */
static void face_swap(RecompCameraSettingRow row)
{
    if (!state.settings.face_each ||
        (row != RECOMP_CAMERA_SETTING_EYES && row != RECOMP_CAMERA_SETTING_MOUTH)) return;
    RecompActorPoseStatus pose;
    recomp_actor_pose_status(&pose);
    const unsigned girl = pose.actor < 4u ? pose.actor : 0u;
    unsigned *own = row == RECOMP_CAMERA_SETTING_EYES ? &state.girl_eyes[girl] : &state.girl_mouth[girl];
    unsigned *shared = row == RECOMP_CAMERA_SETTING_EYES ? &state.settings.eyes : &state.settings.mouth;
    const unsigned held = *own;
    *own = *shared;
    *shared = held;
}

/* The rim and fill lights, on the characters only (the draw adapter adds
   them after the game's lights), placed by the camera: the rim light from
   behind her, above and to one side, so her hair, shoulders and an edge
   catch it; the
   fill from the camera's side and a little above, softening her shadows. */
static void apply_studio_lights(void)
{
    RecompD3dExtraLight lights[RECOMP_D3D_EXTRA_LIGHTS];
    uint32_t count = 0u;
    float target[3], forward[3], right[3];
    const bool flying = state.active && state.started && state.settings_ready;
    if (flying) {
        recomp_free_camera_target(&state.camera, target);
        float length = 0.0f;
        for (unsigned i = 0u; i < 3u; ++i) {
            forward[i] = target[i] - state.camera.position[i];
            length += forward[i] * forward[i];
        }
        length = sqrtf(length);
        if (length > 1e-6f)
            for (unsigned i = 0u; i < 3u; ++i) forward[i] /= length;
        /* Level and square to the line of sight, to one side. */
        right[0] = forward[2];
        right[1] = 0.0f;
        right[2] = -forward[0];
        float rgb[3];
        if (length > 1e-6f && recomp_camera_settings_rim(&state.settings, rgb)) {
            /* Behind her (level, whatever the camera's pitch), above and a
               little to the side: lit only by diffuse light, edges catch it
               where they turn up or away. */
            RecompD3dExtraLight *rim = &lights[count++];
            const float level = sqrtf(forward[0] * forward[0] + forward[2] * forward[2]);
            const float back[2] = {level > 1e-4f ? forward[0] / level : 0.0f, level > 1e-4f ? forward[2] / level : 0.0f};
            const float toward[3] = {0.7f * back[0] + 0.5f * right[0], 0.9f, 0.7f * back[1] + 0.5f * right[2]};
            const float size = sqrtf(toward[0] * toward[0] + toward[1] * toward[1] + toward[2] * toward[2]);
            for (unsigned i = 0u; i < 3u; ++i) {
                rim->toward[i] = toward[i] / size;
                rim->color[i] = rgb[i];
            }
        }
        if (length > 1e-6f && state.settings.fill_light != 0u) {
            RecompD3dExtraLight *fill = &lights[count++];
            const float toward[3] = {-forward[0] + right[0], -forward[1] + 0.3f, -forward[2] + right[2]};
            const float size = sqrtf(toward[0] * toward[0] + toward[1] * toward[1] + toward[2] * toward[2]);
            const float strength = 0.1f * (float)state.settings.fill_light;
            for (unsigned i = 0u; i < 3u; ++i) {
                fill->toward[i] = toward[i] / size;
                fill->color[i] = strength;
            }
        }
    }
    recomp_d3d_draw_set_extra_lights(lights, count);
}

/* Free flight, and with Track her on the chosen girl kept in view: the
   camera aims at her (just above her hips), and following, it moves with her
   too, at the place it was flown to from her. The point she is tracked by
   is smoothed (TRACK_RATE a second), so her steps do not shake the picture. */
static void step_tracked(const RecompFreeCameraControls *controls, float seconds)
{
    float hips[3];
    const bool track = state.settings.track != 0u && recomp_actor_pose_focus(hips);
    if (!track) {
        state.tracking = state.following = false;
        recomp_free_camera_step(&state.camera, controls, seconds);
        return;
    }
    hips[1] += FRAME_LIFT;
    if (!state.tracking) memcpy(state.track_point, hips, sizeof hips);
    const float k = 1.0f - expf(-TRACK_RATE * seconds);
    for (unsigned i = 0u; i < 3u; ++i) state.track_point[i] += (hips[i] - state.track_point[i]) * k;
    state.tracking = true;
    const bool follow = state.settings.track == 2u;
    if (follow && !state.following)
        for (unsigned i = 0u; i < 3u; ++i) state.track_offset[i] = state.camera.position[i] - state.track_point[i];
    state.following = follow;
    if (follow)
        for (unsigned i = 0u; i < 3u; ++i) state.camera.position[i] = state.track_point[i] + state.track_offset[i];
    recomp_free_camera_step(&state.camera, controls, seconds);
    if (follow)
        for (unsigned i = 0u; i < 3u; ++i) state.track_offset[i] = state.camera.position[i] - state.track_point[i];
    recomp_free_camera_look_at(&state.camera, state.track_point);
}

static bool same_matrix(const float a[16], const float b[16])
{
    for (unsigned i = 0u; i < 16u; ++i) {
        float size = fabsf(a[i]) > 1.0f ? fabsf(a[i]) : 1.0f;
        if (fabsf(a[i] - b[i]) > 1e-4f * size) return false;
    }
    return true;
}

/* Puts `matrix` at `address` when it holds `old`. */
static bool replace_matrix(uint32_t address, const float old[16], const float matrix[16])
{
    float current[16];
    recomp_guest_load(current, address, sizeof current);
    if (!same_matrix(current, old)) return false;
    recomp_guest_store(address, matrix, sizeof current);
    return true;
}

/* Some scenes build the camera in a task that does not draw (the pool's
   hopping game), which a frozen scene skips, so the view would stop with
   it. At the start of each frozen frame, if no camera was built in the last
   one, the free camera builds its object itself. That task also copied the
   view and projection to the globals the drawing task hands the device
   (0x004D6F20 and 0x004D95F0, copied by 0x001221E0), so where they, and the
   device, still hold the object's last matrices the new ones replace them. */
static void frame_started(void)
{
    /* Slow motion blends poses of its own; stepped-back poses stay as they were. */
    if (state.active && state.started && !state.slowed) recomp_actor_pose_look_while_frozen();
    if (!state.active || !state.started || !recomp_scene_freeze_holding()) return;
    uint32_t frame = recomp_d3d_frame_adapter_swap_counter();
    if (state.game_built_frame + 1u >= frame) return;
    const RecompRegisters registers = recomp_runtime.registers;
    RecompFpuContext fpu;
    recomp_fpu_context_save(&fpu);
    recomp_runtime.registers.esp -= 4u;
    *recomp_memory_u32(recomp_runtime.registers.esp) = 0u;
    const uint32_t object = CAMERA_FIRST + state.slot * CAMERA_STRIDE;
    float old_view[16], old_projection[16], view[16], projection[16];
    recomp_guest_load(old_view, object + CAMERA_VIEW, sizeof old_view);
    recomp_guest_load(old_projection, object + CAMERA_PROJECTION, sizeof old_projection);
    recomp_runtime.registers.eax = object;
    state.fallback = true;
    build_camera();
    state.fallback = false;
    recomp_guest_load(view, object + CAMERA_VIEW, sizeof view);
    recomp_guest_load(projection, object + CAMERA_PROJECTION, sizeof projection);
    bool replaced = recomp_d3d_draw_replace_view(old_view, view, projection);
    bool copied = replace_matrix(SCENE_VIEW, old_view, view);
    copied = replace_matrix(SCENE_PROJECTION, old_projection, projection) && copied;
    if (tracing(frame)) {
        ++state.trace_lines;
        fprintf(stderr, "recomp free camera trace: frame=%u frozen, so built here; device view %s; "
            "scene copies %s; last frame's draws with each object's view %u %u %u %u %u\n",
            frame, replaced ? "replaced" : "left (not the object's)",
            copied ? "replaced" : "not both the object's", state.drawn_before[0],
            state.drawn_before[1], state.drawn_before[2], state.drawn_before[3], state.drawn_before[4]);
    }
    recomp_runtime.registers = registers;
    recomp_fpu_context_restore(&fpu);
}

RecompFunction recomp_free_camera_lookup_manual(uint32_t guest_address)
{
    if (!enabled())
        return recomp_camera_effects_skin_wanted() ? recomp_actor_pose_lookup_character(guest_address) : NULL;
    if (guest_address == BUILD_CAMERA) return build_camera;
    return recomp_actor_pose_lookup_manual(guest_address);
}
void recomp_free_camera_set_active(bool active)
{
    if (enabled()) set_active(active);
}

bool recomp_free_camera_enabled(void)
{
    return enabled();
}

bool recomp_free_camera_active(void)
{
    return state.active;
}

void recomp_free_camera_face_character(void)
{
    state.aim_pending = true;
}

void recomp_free_camera_frame_character(void)
{
    state.frame_pending = true;
    state.save_home = true;
}

static void fill_overlay(RecompOverlayText *text, bool viewer)
{
    const char *shot = texture_keys() ? "Shift+F12" : "F12";
    char hint[RECOMP_OVERLAY_HINT_SIZE];
    recomp_overlay_clear(text);
    if (state.panel_hidden) return;
    if (!state.active) {
        if (!recomp_scene_freeze_active()) return;
        recomp_overlay_set_title(text, "PAUSED");
        snprintf(hint, sizeof hint,
            "P resume  |  . step  |  H clean view  |  %s screenshot  |  F1 free camera", shot);
        recomp_overlay_set_hint(text, state.controller_hints > 0 ? "P resume  |  R3 free camera" : hint);
        return;
    }

    if (state.settings_open) {
        recomp_overlay_set_title(text, "CAMERA SETTINGS");
        const RecompCameraSettingsPage *page = recomp_camera_settings_page(state.setting_page);
        recomp_overlay_add_line(text, "%s Page: %s (%u of %u, left/right turns)", state.setting_line == 0 ? ">" : " ",
            page->name, state.setting_page + 1u, recomp_camera_settings_page_count());
        for (unsigned line = 0u; line < page->count; ++line) {
            const int row = (int)page->rows[line];
            char value[RECOMP_OVERLAY_LINE_SIZE];
            face_swap((RecompCameraSettingRow)row);
            recomp_camera_settings_describe(&state.settings, (RecompCameraSettingRow)row, state.camera.fov,
                state.character_focus, value, sizeof value);
            face_swap((RecompCameraSettingRow)row);
            if (row == RECOMP_CAMERA_SETTING_PATH)
                snprintf(value, sizeof value, "Camera path: %u views (right adds this view, left removes)", state.path.count);
            else if (row == RECOMP_CAMERA_SETTING_POSE_SLOT || row == RECOMP_CAMERA_SETTING_POSE_KEEP ||
                     row == RECOMP_CAMERA_SETTING_POSE_HOLD)
                describe_pose_row((RecompCameraSettingRow)row, value, sizeof value);
            else if (row == RECOMP_CAMERA_SETTING_PATH_PLAY && state.playing)
                snprintf(value, sizeof value, "Playing %u%%%s (left stops)",
                    100u * state.play_frames / (60u * state.settings.path_seconds * (state.blur ? state.blur : 1u)),
                    state.settings.record ? ", recording" : "");
            recomp_overlay_add_line(text, "%s %s", row == state.setting_row ? ">" : " ", value);
        }
        recomp_overlay_set_hint(text, state.controller_hints > 0
            ? "Back close  |  D-pad up/down choose  |  D-pad left/right change  |  sticks still fly"
            : "Tab close  |  up/down choose  |  left/right change  |  WASD and right-drag still fly");
        return;
    }

    RecompActorPoseStatus pose;
    char name[48];
    recomp_actor_pose_status(&pose);
    recomp_overlay_set_title(text, viewer ? "MODEL VIEWER" : "FREE CAMERA");
    if (pose.active) {
        recomp_actor_pose_name(pose.actor, name, sizeof name);
        recomp_overlay_add_line(text, "%s", name);
        if (pose.browsing) {
            recomp_overlay_add_line(text, "Animation %u of %u (clip %04X)",
                (unsigned)pose.browse_position, (unsigned)pose.browse_count, (unsigned)pose.browse_clip);
        } else if (pose.holding) {
            recomp_overlay_add_line(text, "Animation %u of %u (clip %04X)",
                (unsigned)pose.position, (unsigned)pose.count, (unsigned)pose.clip);
        } else {
            recomp_overlay_add_line(text, "Animation: the game's own");
        }
        uint16_t suit = recomp_suit_switch_shown(pose.actor);
        uint32_t place = 0u, count = 0u;
        if (suit != 0u) {
            char suit_name[48];
            recomp_suit_switch_name(suit, suit_name, sizeof suit_name);
            recomp_suit_switch_place(pose.actor, suit, &place, &count);
            recomp_overlay_add_line(text, "Swimsuit %u of %u: %s%s", (unsigned)place,
                (unsigned)count, suit_name, recomp_suit_switch_busy() ? " (changing)" : "");
        }
    }
    /* Slow motion and motion blur run a frozen scene a step at a time; say
       what the picture is doing, not that it is frozen. */
    if (state.slowed && state.slow_paused) {
        recomp_overlay_add_line(text, "Slow motion paused");
    } else if (state.slowed) {
        const unsigned speed = 1u << state.settings.slow;
        if (speed > 1u && state.blur > 1u)
            recomp_overlay_add_line(text, "Slow motion 1/%u, motion blur", speed);
        else if (speed > 1u)
            recomp_overlay_add_line(text, "Slow motion 1/%u", speed);
        else
            recomp_overlay_add_line(text, "Running, motion blur");
    } else {
        recomp_overlay_add_line(text, "%s", recomp_scene_freeze_active() ? "Frozen" : "Running");
    }
    if (state.controller_hints > 0) {
        recomp_overlay_set_hint(text, viewer
            ? "R3 leave  |  Start freeze  |  RB step  |  LB clean view  |  Back + A screenshot  |  Y reset\n"
              "Left stick move  |  LT RT down/up  |  Right stick look\n"
              "D-pad left/right animation  |  D-pad up/down swimsuit  |  Back camera settings"
            : "R3 off  |  Start freeze  |  RB step  |  LB clean view  |  Back + A screenshot  |  Y reset\n"
              "Left stick move  |  LT RT down/up  |  Right stick look  |  L3 next girl\n"
              "D-pad left/right animation  |  D-pad up/down swimsuit  |  Back camera settings");
    } else {
        snprintf(hint, sizeof hint, viewer
            ? "F1 leave  |  P freeze  |  . step  |  H clean view  |  %s screenshot  |  R reset\n"
              "WASD QE move  |  arrows / right-drag look  |  Z X zoom\n"
              "[ ] animation  |  - = swimsuit  |  Tab camera settings"
            : "F1 off  |  P freeze  |  . step  |  H clean view  |  %s screenshot  |  R reset\n"
              "WASD QE move  |  arrows / right-drag look  |  Z X zoom  |  \\ next girl\n"
              "[ ] animation  |  - = swimsuit  |  Tab camera settings", shot);
        recomp_overlay_set_hint(text, hint);
    }
}

/* Before a frozen scene runs on: the scene goes back to exactly where the
   game left it, and is sampled again once it has run. */
static void scene_runs(void)
{
    /* The root blocks stepped back hold each character's whole state: the
       scene goes on from the newest frame, not the one shown. */
    const unsigned back = recomp_actor_pose_history_back();
    if (back != 0u) recomp_actor_pose_history_step((int)back);
    recomp_slow_motion_restore();
    state.sample_due = true;
    state.sample_continues = state.slowed;
}

/* Slow motion: the scene is frozen and stepped one frame in 2, 4 or 8. At
   60 Hz it is smooth: each step runs unseen, and each frame shown is part of
   the way from the step before to the last one (slow_motion.c), so a step
   is shown a step late. A girl hidden or shown since the sample is left. */
static void step_slow_motion(void)
{
    const bool smooth = recomp_game_requested_hz() == 0u;
    /* Motion blur shows the average of `blur` pictures a frame, each part of
       the way on: slow motion's blend, `blur` times finer. */
    unsigned blur = state.active && state.settings_ready && smooth && state.settings.motion_blur != 0u ?
        2u << state.settings.motion_blur : 1u;
    const unsigned every = (state.active && state.settings_ready ? 1u << state.settings.slow : 1u) * blur;
    state.blur = blur;
    if (every > 1u) {
        /* Anything else that let the scene run leaves slow motion on: slow
           it again. */
        if (state.slowed && !recomp_scene_freeze_active()) {
            state.slowed = false;
            recomp_slow_motion_reset();
        }
        if (!state.slowed) {
            recomp_scene_freeze_set(true);
            state.slowed = true;
            state.slow_paused = false;
            state.slow_frames = 0u;
            state.sample_due = true;
            state.sample_continues = false;
        }
        /* Stepping back through poses shows them as they were. */
        if (!recomp_scene_freeze_holding() || recomp_actor_pose_history_back() != 0u) return;
        if (state.sample_due && smooth) {
            recomp_slow_motion_commit(state.sample_continues);
            state.sampled_hidden = state.settings_hidden;
            state.slow_frames = 0u;
        }
        state.sample_due = false;
        if (state.slow_paused) return;
        if (state.slow_frames >= every) {
            state.slow_frames = 0u;
            recomp_scene_freeze_step();
            if (smooth) {
                recomp_game_hide_pass();
                state.unseen_frame = recomp_d3d_frame_adapter_swap_counter();
            }
            return;
        }
        ++state.slow_frames;
        if (smooth) recomp_slow_motion_apply((float)state.slow_frames / (float)every,
            state.settings_hidden ^ state.sampled_hidden);
        if (blur > 1u) recomp_d3d_frame_adapter_set_blur((state.slow_frames - 1u) % blur, blur);
    } else if (state.slowed) {
        recomp_scene_freeze_set(false);
        state.slowed = false;
        state.slow_paused = false;
        recomp_slow_motion_reset();
    }
}

/* The settings hide the ball (in a match) and every character but the
   chosen one while the camera flies; the model viewer does its own hiding. */
/* The character nearest the middle of the view, by her hips; false for none
   within the field of view. */
static bool centred_actor(uint32_t *chosen)
{
    float target[3], forward[3], length = 0.0f;
    recomp_free_camera_target(&state.camera, target);
    for (unsigned i = 0u; i < 3u; ++i) {
        forward[i] = target[i] - state.camera.position[i];
        length += forward[i] * forward[i];
    }
    length = sqrtf(length);
    if (!(length > 0.0f)) return false;
    float best = cosf(0.5f * state.camera.fov);
    bool found = false;
    for (uint32_t actor = 0u; actor < 4u; ++actor) {
        float hips[3], ahead = 0.0f, distance = 0.0f;
        if (!recomp_actor_pose_hips(actor, hips)) continue;
        for (unsigned i = 0u; i < 3u; ++i) {
            const float offset = hips[i] - state.camera.position[i];
            ahead += offset * forward[i] / length;
            distance += offset * offset;
        }
        distance = sqrtf(distance);
        const float centred = distance > 0.0f ? ahead / distance : -1.0f;
        if (centred > best) {
            best = centred;
            *chosen = actor;
            found = true;
        }
    }
    return found;
}

static void apply_hiding(void)
{
    recomp_viewer_request_ball_hidden(state.active && state.settings.hide_ball);
    if (state.viewer) return;
    uint32_t mask = 0u;
    if (state.active && state.settings.hide_others) {
        /* On the way in, the girl in view becomes the chosen one. */
        uint32_t actor;
        if (state.settings_hidden == 0u && centred_actor(&actor)) recomp_actor_pose_choose(actor);
        RecompActorPoseStatus pose;
        recomp_actor_pose_status(&pose);
        mask = 0xFu & ~(1u << pose.actor);
    }
    if (mask != state.settings_hidden) recomp_actor_pose_set_hidden(mask);
    state.settings_hidden = mask;
}

void recomp_free_camera_overlay(bool viewer)
{
    RecompOverlayText text;
    state.viewer = viewer;
    fill_overlay(&text, viewer);
    /* The composition grid shows while flying, clean view or not. */
    if (state.active && state.settings_ready && state.settings.grid != 0u) {
        RecompCameraEffects framing;
        recomp_camera_settings_effects(&state.settings, state.camera.fov, 0.0f, &framing);
        text.grid = state.settings.grid;
        text.bars = framing.letterbox;
    }
    recomp_overlay_publish(&text);
    /* The effects show only while flying. */
    RecompCameraEffects effects;
    if (!state.settings_ready) settings_begin();
    recomp_camera_settings_effects(&state.settings, state.camera.fov, state.character_focus, &effects);
    if (state.playing && state.play_looked) {
        /* A playing path's look, through this frame's lens; screenshots keep
           the settings' background. */
        const bool transparent = effects.transparent;
        const float occlusion = effects.occlusion;
        effects = state.play_look;
        effects.fov = state.camera.fov;
        effects.transparent = transparent;
        effects.occlusion = occlusion;
        effects.active = effects.active || occlusion > 0.0f;
    }
    effects.active = effects.active && state.active && state.started;
    effects.recording = state.playing && state.settings.record && state.active;
    step_slow_motion();
    recomp_actor_pose_set_look(state.active && state.started && state.settings.eye_contact != 0u,
        state.settings.eye_contact == 2u, state.camera.position);
    if (!(state.active && state.started)) {
        effects.hour = -1.0f;
        effects.light_angle = 0.0f;
        effects.light_strength = 1.0f;
        effects.backdrop = false;
    }
    recomp_camera_effects_publish(&effects);
    keep_settings();
    apply_studio_lights();
    int eyes[4] = {-1, -1, -1, -1}, mouth[4] = {-1, -1, -1, -1};
    for (unsigned girl = 0u; girl < 4u && state.active && state.started; ++girl) {
        // Her type picks her own shapes (camera_settings.c).
        const unsigned type = *recomp_memory(0x004d6ff1u + girl * 0xbcu, 1u);
        if (state.settings.face_each)
            recomp_camera_settings_face_shapes(state.girl_eyes[girl], state.girl_mouth[girl], type, &eyes[girl], &mouth[girl]);
        else
            recomp_camera_settings_face(&state.settings, type, &eyes[girl], &mouth[girl]);
    }
    recomp_body_morph_hold_faces(eyes, mouth);
    apply_hiding();
}
#else
RecompFunction recomp_free_camera_lookup_manual(uint32_t guest_address)
{
    (void)guest_address;
    return NULL;
}

void recomp_free_camera_set_active(bool active)
{
    (void)active;
}

bool recomp_free_camera_enabled(void)
{
    return false;
}

bool recomp_free_camera_active(void)
{
    return false;
}

void recomp_free_camera_face_character(void)
{
}

void recomp_free_camera_frame_character(void)
{
}

void recomp_free_camera_overlay(bool viewer)
{
    (void)viewer;
}
#endif
