#define DEBUG 1
#include "corelib.h"
#include <SDL2/SDL.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>

const int SCALE = 2;

void emu_puts(const char* msg) {
    printf("EMU LOG: %s\n", msg);
}

static int modePressed = false;
void sdl_update_keys(const uint8_t *sdlkeys) {
    set_key(BTN_A, sdlkeys[SDL_SCANCODE_C]);      // x
    set_key(BTN_B, sdlkeys[SDL_SCANCODE_V]);      // circle
    set_key(BTN_X, sdlkeys[SDL_SCANCODE_Z]);      // x
    set_key(BTN_Y, sdlkeys[SDL_SCANCODE_X]);      // x

    set_key(BTN_Sel, sdlkeys[SDL_SCANCODE_TAB]);    // Select
    set_key(BTN_Start, sdlkeys[SDL_SCANCODE_RETURN]); // Start
    set_key(BTN_Up, sdlkeys[SDL_SCANCODE_UP]);     // Dpad Up
    set_key(BTN_Down, sdlkeys[SDL_SCANCODE_DOWN]);   // Dpad Down
    set_key(BTN_Left, sdlkeys[SDL_SCANCODE_LEFT]);   // Dpad Left
    set_key(BTN_Right, sdlkeys[SDL_SCANCODE_RIGHT]);   // Dpad Right
    set_key(BTN_L, sdlkeys[SDL_SCANCODE_A]);  // left shoulder
    set_key(BTN_R, sdlkeys[SDL_SCANCODE_S]);  // right shoulder
    set_key(BTN_L2, sdlkeys[SDL_SCANCODE_Q]);
    set_key(BTN_R2, sdlkeys[SDL_SCANCODE_W]);

    if (!modePressed && sdlkeys[SDL_SCANCODE_SPACE]) {
        // Send mode only for a tap.
        set_key(BTN_PICO_MODE, true);
        set_key(BTN_PICO_MODE, false);
    }
    modePressed = sdlkeys[SDL_SCANCODE_SPACE];
}

char run = 1;
const uint8_t *sdlkeys;
char drag = 0;
void input() {
    for (SDL_Event event; SDL_PollEvent(&event);) {
        if (event.type == SDL_QUIT) {
            run = 0;
        }
        if (event.type == SDL_MOUSEBUTTONDOWN) {
            drag = 1;
        } else if (event.type == SDL_MOUSEBUTTONUP) {
            drag = 0;
        }
    }
    if (drag) {
        int x, y;
        SDL_GetMouseState(&x, &y);
        x /= SCALE;
        y /= SCALE;
        set_touch(x, y);
        printf("x,y = %d,%d\n", x, y);
    }
  sdl_update_keys(sdlkeys);

}

size_t slurp(const char*filename, uint8_t* out, size_t capacity) {
    size_t pos = 0;
    int fd = open(filename, 0);
    if (fd == -1) {
        perror("open fail: ");
        exit(1);
    }
    off_t result = lseek(fd, 0, SEEK_END); 
    printf("lseek returned: %ld\n", result);
    printf("capacity is: %ld\n", capacity);
    if (result < 0) {
        perror("seek failed: ");
        exit(1);
    }
    assert(result <= capacity);
    lseek(fd, 0, SEEK_SET);
    while (1) {
        ssize_t count = read(fd, out + pos, capacity - pos);
        if (count == -1) {
            perror("read fail: ");
            exit(1);
        }
        if (count == 0) break;
        pos += count;
    }
    return pos;
}

typedef struct {
    SDL_AudioDeviceID device;
} SoundCard;

void audio_callback(void *userdata, uint8_t* data, int bytes) {
    // printf("attempt to dequeue %d bytes\n", bytes);
    assert(bytes % sizeof(int16_t) == 0);
    apu_sample_variable((int16_t*)data, bytes / sizeof(int16_t));
}

void soundcard_init(SoundCard *snd) {
    SDL_AudioSpec desired;
    desired.freq = SAMPLE_RATE;
    desired.format = AUDIO_S16LSB;   // intel and aarch64 are both LE
    desired.channels = 1;
    desired.samples = 0;
    desired.callback = audio_callback;  // async audio
    SDL_AudioSpec actual;
    int res = SDL_OpenAudio(&desired, &actual);
    assert(res == 0);
    assert(desired.freq == actual.freq);
    // assert(desired.format == actual.format);  // some bits may change
    assert(desired.channels == actual.channels);
    // assert(desired.samples == actual.samples);

    snd->device = 1;
    SDL_PauseAudioDevice(snd->device, 0);
}


void soundcard_queue(SoundCard *snd, uint8_t* data, size_t bytes) {
    if(SDL_QueueAudio(snd->device, (void*)data, bytes) != 0) {
        printf("audio queue fail: '%s'\n", SDL_GetError());
    }
    SDL_PauseAudioDevice(snd->device, 0);
}

const int ROM_BUFFER_BYTES = 500*1024*1024;
const int BYTES_PER_PIXEL=4;
int main(int argc, char **argv) {
  corelib_set_puts(emu_puts);
  sdlkeys = (uint8_t*)SDL_GetKeyboardState(0);

  char skipSave = 0;

  if (argc == 0) {
      puts("usage: main [rom]");
      return 0;
  } else if (argc == 1) {
      printf("usage: %s [rom]\n", argv[0]);
      return 0;
  } else if (argc == 2) {
      // load rom only.
  } else if (argc == 3) {
      // skipsave
      skipSave = 1;
  }

  uint8_t *buffer = (uint8_t*)malloc(ROM_BUFFER_BYTES);
  size_t bytes_read = slurp(argv[1], buffer, ROM_BUFFER_BYTES);
  printf("read bytes: %lu\n", bytes_read);
  init(buffer, bytes_read);

  char save_file[1024];
  snprintf(save_file, 1023, "%s.sav", argv[1]);
  if (skipSave) {
      printf("Skipping ubersave load.\n");
  } else {
      printf("Loading %s\n", save_file);
      load_state(save_file);
  }

  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window *window = 
      SDL_CreateWindow("pico 16", 0, 0, VIDEO_WIDTH*SCALE, VIDEO_HEIGHT*SCALE, SDL_WINDOW_SHOWN);
  SDL_Renderer *renderer = SDL_CreateRenderer(
      window, -1,
      SDL_RENDERER_ACCELERATED);
  SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32,
  // SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                    SDL_TEXTUREACCESS_STREAMING, VIDEO_WIDTH, VIDEO_HEIGHT);
  SoundCard soundcard;
  soundcard_init(&soundcard);
  // soundcard_test();

  // Use wall-clock time (not CPU time) for frame pacing
  auto get_time_us = []() -> uint64_t {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
      return (uint64_t)ts.tv_sec * 1000*1000 + ts.tv_nsec / 1000;
  };
  uint64_t last = get_time_us();
  char title[64];
  int cur_w = 0, cur_h = 0;

  while(run) {
      input();
      frame();

      int w = width();
      int h = height();
      if (w != cur_w || h != cur_h) {
          cur_w = w;
          cur_h = h;
          snprintf(title, sizeof(title), "PicoDS - %d x %d %d hz", w, h, VIDEO_FRAMERATE);
          SDL_SetWindowTitle(window, title);
          SDL_SetWindowSize(window, w * SCALE, h * SCALE);
      }

      SDL_UpdateTexture(texture, 0, framebuffer(), VIDEO_WIDTH * BYTES_PER_PIXEL);
      SDL_Rect src = { 0, 0, cur_w, cur_h };
      SDL_RenderCopy(renderer, texture, &src, 0);
      SDL_RenderPresent(renderer);

      uint64_t delta = 1000*1000 / VIDEO_FRAMERATE;
      uint64_t target = last + delta;
      uint64_t now = get_time_us();
      int64_t delay = target - now;
      if (delay > 0) {
          usleep(delay);
      } else {
          // puts("lagging");
      }
      last = target;
  }
  printf("save file: %s\n", save_file);
  dump_state(save_file);
}
