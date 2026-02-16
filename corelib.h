#pragma once

#include <stdint.h>
#include <stddef.h>

// NDS video dimensions: 256px wide, two 192px screens stacked vertically
#define VIDEO_WIDTH 256
#define VIDEO_HEIGHT 384
#define VIDEO_FRAMERATE 60

enum DisplayMode {
    DISPLAY_COLUMN = 0, // Both screens stacked vertically
    DISPLAY_TOP,        // Top screen only
    DISPLAY_BOTTOM,     // Bottom screen only
};

enum Keys {
    BTN_A = 0,
    BTN_B,
    BTN_Sel,
    BTN_Start,
    BTN_Up,
    BTN_Down,
    BTN_Left,
    BTN_Right,
    BTN_L,  // 8
    BTN_R,  // 9
    BTN_L2, // 10
    BTN_R2, // 11
    BTN_X,  // 12
    BTN_Y,  // 13
    BTN_PICO_MODE, // 14 - cycle display mode
    NUM_KEYS
};

// Function declarations for DS corelib interface
extern void corelib_set_puts(void(*cb)(const char*));

extern "C" void set_key(size_t key, char val);
// set touch relative to video window
extern "C" void set_touch(int x, int y);
extern "C" void init(const uint8_t* data, size_t len);

// Framebuffer returns an RGBA32 buffer of width() * height() pixels.
// Max size is VIDEO_WIDTH * VIDEO_HEIGHT (both screens).
extern "C" const uint8_t *framebuffer();
extern "C" int width();
extern "C" int height();

#ifdef __wasm32__
extern "C" size_t framebuffer_bytes();
extern "C" uint8_t *alloc_rom(size_t bytes);
#endif

extern "C" void frame();

extern "C" void dump_state(const char* save_path);
extern "C" void load_state(const char* save_path);

// APU
const int SAMPLE_RATE = 44100;
const int SAMPLES_PER_FRAME = SAMPLE_RATE / VIDEO_FRAMERATE;
extern "C" void apu_tick_60hz();
extern "C" void apu_sample_60hz(int16_t *output);
extern "C" long apu_sample_variable(int16_t *output, int32_t frames);
