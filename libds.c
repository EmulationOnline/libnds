#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <memory>
#include <cstdarg>
#include <thread>
#include <mutex>
#include <semaphore>
#include <chrono>
#include <functional>

#include "corelib.h"
#include "ring.h"

// melonDS headers
#include <NDS.h>
#include <NDSCart.h>
#include <Args.h>
#include <GPU.h>
#include <SPU.h>
#include <SPI_Firmware.h>
#include <FreeBIOS.h>
#include <Platform.h>

#ifndef __wasm32__
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <assert.h>
#endif

const int PANEL_HEIGHT = 192;
struct ring_i16 ring_;

// Key mask: bit=1 means released, bit=0 means pressed
// Bit layout: 0=A 1=B 2=Sel 3=Start 4=Right 5=Left 6=Up 7=Down 8=R 9=L 10=X 11=Y
static uint32_t keymask_ = 0xFFF; // all released

static int key_to_bit(size_t key) {
    switch (key) {
        case BTN_A:     return 0;
        case BTN_B:     return 1;
        case BTN_Sel:   return 2;
        case BTN_Start: return 3;
        case BTN_Right: return 4;
        case BTN_Left:  return 5;
        case BTN_Up:    return 6;
        case BTN_Down:  return 7;
        case BTN_R:     return 8;
        case BTN_L:     return 9;
        case BTN_X:     return 10;
        case BTN_Y:     return 11;
        default:        return -1;
    }
}

// Global melonDS emulator instance
static std::unique_ptr<melonDS::NDS> nds = nullptr;

#define puts(arg) do { if (emu_puts_cb) emu_puts_cb(arg); } while(0)
static void (*emu_puts_cb)(const char *) = NULL;

// =========================================================================
// melonDS Platform implementation
// melonDS requires these functions to be defined by the frontend.
// =========================================================================
namespace melonDS::Platform
{

void SignalStop(StopReason reason, void* userdata) {
    printf("melonDS SignalStop: reason=%d\n", (int)reason);
}

void Log(LogLevel level, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

// --- File I/O (minimal stubs - we don't need file access for basic emulation) ---

struct FileHandle {
    FILE* file;
};

FileHandle* OpenFile(const std::string& path, FileMode mode) {
    const char* m = "rb";
    if (mode & FileMode::Write) {
        if (mode & FileMode::Read) m = "r+b";
        else if (mode & FileMode::Preserve) m = "r+b";
        else m = "wb";
    }
    FILE* f = fopen(path.c_str(), m);
    if (!f && (mode & FileMode::Write) && !(mode & FileMode::NoCreate)) {
        f = fopen(path.c_str(), "w+b");
    }
    if (!f) return nullptr;
    auto* h = new FileHandle;
    h->file = f;
    return h;
}

FileHandle* OpenLocalFile(const std::string& path, FileMode mode) {
    return OpenFile(path, mode);
}

bool FileExists(const std::string& name) {
    FILE* f = fopen(name.c_str(), "rb");
    if (f) { fclose(f); return true; }
    return false;
}

bool LocalFileExists(const std::string& name) {
    return FileExists(name);
}

bool CheckFileWritable(const std::string& filepath) {
    FILE* f = fopen(filepath.c_str(), "ab");
    if (f) { fclose(f); return true; }
    return false;
}

bool CheckLocalFileWritable(const std::string& filepath) {
    return CheckFileWritable(filepath);
}

bool CloseFile(FileHandle* file) {
    if (!file) return false;
    fclose(file->file);
    delete file;
    return true;
}

bool IsEndOfFile(FileHandle* file) {
    return file ? (feof(file->file) != 0) : false;
}

bool FileReadLine(char* str, int count, FileHandle* file) {
    if (!file) return false;
    return fgets(str, count, file->file) != nullptr;
}

bool FileSeek(FileHandle* file, s64 offset, FileSeekOrigin origin) {
    if (!file) return false;
    int whence = SEEK_SET;
    if (origin == FileSeekOrigin::Current) whence = SEEK_CUR;
    else if (origin == FileSeekOrigin::End) whence = SEEK_END;
    return fseek(file->file, offset, whence) == 0;
}

void FileRewind(FileHandle* file) {
    if (file) rewind(file->file);
}

u64 FileRead(void* data, u64 size, u64 count, FileHandle* file) {
    if (!file) return 0;
    return fread(data, size, count, file->file);
}

bool FileFlush(FileHandle* file) {
    if (!file) return false;
    return fflush(file->file) == 0;
}

u64 FileWrite(const void* data, u64 size, u64 count, FileHandle* file) {
    if (!file) return 0;
    return fwrite(data, size, count, file->file);
}

u64 FileWriteFormatted(FileHandle* file, const char* fmt, ...) {
    if (!file) return 0;
    va_list args;
    va_start(args, fmt);
    u64 ret = vfprintf(file->file, fmt, args);
    va_end(args);
    return ret;
}

u64 FileLength(FileHandle* file) {
    if (!file) return 0;
    long pos = ftell(file->file);
    fseek(file->file, 0, SEEK_END);
    long len = ftell(file->file);
    fseek(file->file, pos, SEEK_SET);
    return len;
}

std::string GetLocalFilePath(const std::string& filename) {
    return filename;
}

// --- Threading ---

struct Thread {
    std::thread t;
};

Thread* Thread_Create(std::function<void()> func) {
    auto* t = new Thread;
    t->t = std::thread(func);
    return t;
}

void Thread_Free(Thread* thread) {
    if (thread) {
        if (thread->t.joinable()) thread->t.join();
        delete thread;
    }
}

void Thread_Wait(Thread* thread) {
    if (thread && thread->t.joinable()) thread->t.join();
}

struct Semaphore {
    std::counting_semaphore<> sem{0};
};

Semaphore* Semaphore_Create() { return new Semaphore; }
void Semaphore_Free(Semaphore* s) { delete s; }
void Semaphore_Reset(Semaphore* s) { while (s->sem.try_acquire()); }
void Semaphore_Wait(Semaphore* s) { s->sem.acquire(); }
bool Semaphore_TryWait(Semaphore* s, int timeout_ms) {
    if (!timeout_ms) return s->sem.try_acquire();
    return s->sem.try_acquire_for(std::chrono::milliseconds(timeout_ms));
}
void Semaphore_Post(Semaphore* s, int count) { s->sem.release(count); }

struct Mutex {
    std::mutex m;
};

Mutex* Mutex_Create() { return new Mutex; }
void Mutex_Free(Mutex* m) { delete m; }
void Mutex_Lock(Mutex* m) { m->m.lock(); }
void Mutex_Unlock(Mutex* m) { m->m.unlock(); }
bool Mutex_TryLock(Mutex* m) { return m->m.try_lock(); }

void Sleep(u64 usecs) {
    std::this_thread::sleep_for(std::chrono::microseconds(usecs));
}

u64 GetMSCount() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

u64 GetUSCount() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// --- Save callbacks (no-ops for now) ---

void WriteNDSSave(const u8*, u32, u32, u32, void*) {}
void WriteGBASave(const u8*, u32, u32, u32, void*) {}
void WriteFirmware(const Firmware&, u32, u32, void*) {}
void WriteDateTime(int, int, int, int, int, int, void*) {}

// --- Multiplayer (no-ops) ---

void MP_Begin(void*) {}
void MP_End(void*) {}
int MP_SendPacket(u8*, int, u64, void*) { return 0; }
int MP_RecvPacket(u8*, u64*, void*) { return 0; }
int MP_SendCmd(u8*, int, u64, void*) { return 0; }
int MP_SendReply(u8*, int, u64, u16, void*) { return 0; }
int MP_SendAck(u8*, int, u64, void*) { return 0; }
int MP_RecvHostPacket(u8*, u64*, void*) { return 0; }
u16 MP_RecvReplies(u8*, u64, u16, void*) { return 0; }

// --- Network (no-ops) ---

int Net_SendPacket(u8*, int, void*) { return 0; }
int Net_RecvPacket(u8*, void*) { return 0; }

// --- Camera (no-ops) ---

void Camera_Start(int, void*) {}
void Camera_Stop(int, void*) {}
void Camera_CaptureFrame(int, u32*, int, int, bool, void*) {}

// --- Rumble (no-ops) ---

void Addon_RumbleStart(u32, void*) {}
void Addon_RumbleStop(void*) {}

// --- Dynamic library (no-ops) ---

struct DynamicLibrary {};
DynamicLibrary* DynamicLibrary_Load(const char*) { return nullptr; }
void DynamicLibrary_Unload(DynamicLibrary*) {}
void* DynamicLibrary_LoadFunction(DynamicLibrary*, const char*) { return nullptr; }

} // namespace melonDS::Platform

// =========================================================================
// Corelib interface implementation
// =========================================================================

__attribute__((visibility("default")))
void corelib_set_puts(void (*cb)(const char *)) {
    emu_puts_cb = cb;
}

// Current display mode and dimensions
static DisplayMode display_mode_ = DISPLAY_COLUMN;
static int width_ = VIDEO_WIDTH;
static int height_ = VIDEO_HEIGHT;

extern "C" __attribute__((visibility("default")))
int width() { return width_; }

extern "C" __attribute__((visibility("default")))
int height() { return height_; }

uint32_t fbuffer_[VIDEO_WIDTH * VIDEO_HEIGHT];

extern "C" __attribute__((visibility("default")))
const uint8_t *framebuffer() {
    if (!nds) return (uint8_t*)fbuffer_;

    int front = nds->GPU.FrontBuffer;
    const uint32_t* top = nds->GPU.Framebuffer[front][0].get();
    const uint32_t* bot = nds->GPU.Framebuffer[front][1].get();
    if (!top || !bot) return (uint8_t*)fbuffer_;

    switch (display_mode_) {
        case DISPLAY_COLUMN:
            memcpy(fbuffer_, top, 256 * 192 * sizeof(uint32_t));
            memcpy(fbuffer_ + 256 * 192, bot, 256 * 192 * sizeof(uint32_t));
            break;
        case DISPLAY_TOP:
            memcpy(fbuffer_, top, 256 * 192 * sizeof(uint32_t));
            break;
        case DISPLAY_BOTTOM:
            memcpy(fbuffer_, bot, 256 * 192 * sizeof(uint32_t));
            break;
    }

    return (uint8_t*)fbuffer_;
}

// Audio resampling: NDS SPU (~32728 Hz stereo) -> 44100 Hz mono
static constexpr double NDS_SAMPLE_RATE = 33513982.0 / 1024.0;
static double resample_pos_ = 0.0;

static void push_audio() {
    // 1. Read stereo s16 from SPU
    int16_t stereo_buf[2048 * 2];
    int available = nds->SPU.GetOutputSize();
    if (available <= 0) return;
    if (available > 2048) available = 2048;
    int frames_read = nds->SPU.ReadOutput(stereo_buf, available);
    if (frames_read <= 0) return;

    // 2. Downmix stereo to mono
    int16_t mono_buf[2048];
    for (int i = 0; i < frames_read; i++) {
        mono_buf[i] = (int16_t)(((int32_t)stereo_buf[i * 2] + stereo_buf[i * 2 + 1]) / 2);
    }

    // 3. Linear interpolation resample to SAMPLE_RATE (44100)
    const double ratio = NDS_SAMPLE_RATE / (double)SAMPLE_RATE;
    int16_t resampled[2048];
    int out_count = 0;

    while (resample_pos_ < frames_read - 1 && out_count < 2048) {
        int idx = (int)resample_pos_;
        double frac = resample_pos_ - idx;
        resampled[out_count++] = (int16_t)(mono_buf[idx] * (1.0 - frac) + mono_buf[idx + 1] * frac);
        resample_pos_ += ratio;
    }

    // Carry fractional position into next frame
    resample_pos_ -= frames_read;
    if (resample_pos_ < 0.0) resample_pos_ = 0.0;

    ring_push(&ring_, resampled, out_count);
}

// touches are relative to the upper left of video. It is assumed that if set_touch
// isnt called on the frame, then the touch has been released.
void untouch() {
    if (!nds) return;
    nds->ReleaseScreen();
}
extern "C" __attribute__((visibility("default")))
void set_touch(int x, int y) {
    if (!nds) return;

    switch (display_mode_) {
        case DISPLAY_COLUMN:
            y -= PANEL_HEIGHT;
            break;
        case DISPLAY_TOP:
            // no touchpad
            return;
        case DISPLAY_BOTTOM:
            // unmodified
            break;
    }

    if (y < 0 || y > PANEL_HEIGHT) return;
    if (x < 0 || x > VIDEO_WIDTH) return;
    // MelonDsDs::InputPullResult result;
    // result.PointerPosition.x = x;
    // result.PointerPosition.y = y;
    // result.PointerPressed = 1;
    // nds->GetInputState().PointerState.Update(&result);
    nds->TouchScreen(x, y);
}

extern "C" __attribute__((visibility("default")))
void frame() {
    if (!nds) return;
    nds->RunFrame();
    untouch();
    push_audio();
}

static const char* display_mode_name(DisplayMode mode) {
    switch (mode) {
        case DISPLAY_COLUMN: return "Column";
        case DISPLAY_TOP:    return "Top";
        case DISPLAY_BOTTOM: return "Bottom";
        default:             return "Unknown";
    }
}

static void set_display_mode(DisplayMode mode) {
    display_mode_ = mode;
    switch (mode) {
        case DISPLAY_COLUMN:
            width_ = VIDEO_WIDTH;
            height_ = VIDEO_HEIGHT;
            break;
        case DISPLAY_TOP:
        case DISPLAY_BOTTOM:
            width_ = VIDEO_WIDTH;
            height_ = VIDEO_HEIGHT / 2;
            break;
    }
    printf("Display mode: %s (%dx%d)\n", display_mode_name(mode), width_, height_);
}

extern "C" __attribute__((visibility("default")))
void set_key(size_t key, char val) {
    if (!nds) return;
    if (key == BTN_PICO_MODE) {
        if (val) set_display_mode((DisplayMode)((display_mode_ + 1) % 3));
        return;
    }
    int bit = key_to_bit(key);
    if (bit < 0) return;
    if (val)
        keymask_ &= ~(1 << bit);  // pressed: clear bit
    else
        keymask_ |= (1 << bit);   // released: set bit
    nds->SetKeyMask(keymask_);
}


static void cleanup() {
    puts("cleanup");
    nds.reset();
}

extern "C" __attribute__((visibility("default")))
void init(const uint8_t* data, size_t len) {
    puts("melonDS core init()");

    if (nds) {
        cleanup();
    }
    keymask_ = 0xFFF; // all keys released
    ring_init(&ring_);

    // 1. Parse the ROM into an NDSCart
    puts("parsing rom");
    auto cart = melonDS::NDSCart::ParseROM(data, (melonDS::u32)len);
    if (!cart) {
        printf("Failed to parse NDS ROM (%zu bytes)\n", len);
        return;
    }
    printf("Parsed NDS ROM: %zu bytes\n", len);

    // 2. Build NDSArgs with defaults (FreeBIOS, generated firmware, software renderer)
    melonDS::NDSArgs args {};
    args.NDSROM = std::move(cart);
    // ARM9/ARM7 BIOS default to FreeBIOS (set in Args.h defaults)
    // Firmware defaults to generated NDS firmware (consoletype 0 = DS)
    // JIT defaults to enabled (but we built without JIT, so it's ignored)
    args.JIT = std::nullopt; // disable JIT explicitly

    // 3. Create the NDS instance
    nds = std::make_unique<melonDS::NDS>(std::move(args));
    melonDS::NDS::Current = nds.get();

    // 4. Reset and set up direct boot (skip firmware boot screen)
    nds->Reset();
    if (nds->GetNDSCart() && nds->NeedsDirectBoot()) {
        nds->SetupDirectBoot("rom.nds");
    }
    nds->Start();

    printf("melonDS initialized successfully\n");
    puts("melonDS core initialized successfully");
}

extern "C" __attribute__((visibility("default")))
long apu_sample_variable(int16_t *output, int32_t frames) {
    if (!nds) return 0;
    size_t received = ring_pull(&ring_, output, frames);
    if (received < (size_t)frames) {
        for (size_t i = received; i < (size_t)frames; i++) {
            output[i] = 0;
        }
    }
    return received;
}

int save_str(uint8_t* dest, int capacity) {
    if (!nds) return 0;
    if (!dest) {
        // Dry run: measure size
        melonDS::Savestate state;
        nds->DoSavestate(&state);
        return state.Length();
    }
    melonDS::Savestate state(dest, capacity, true);
    if (!nds->DoSavestate(&state) || state.Error) return -1;
    return state.Length();
}

void load_str(int len, const uint8_t* src) {
    if (!nds || !src || len <= 0) return;
    melonDS::Savestate state(const_cast<uint8_t*>(src), len, false);
    if (!nds->DoSavestate(&state) || state.Error) {
        printf("Failed to load savestate (%d bytes)\n", len);
    }
}

#ifndef __wasm32__

extern "C" __attribute__((visibility("default")))
void save(int fd) {
    if (!nds) return;
    int est = save_str(NULL, 0);
    if (est <= 0) return;
    uint8_t *buffer = (uint8_t*)malloc(est);
    int size = save_str(buffer, est);
    assert(size <= est);
    const uint8_t* wr = buffer;
    while(size > 0) {
        ssize_t count = write(fd, wr, size);
        if (count < 0) { perror("write failed"); break; }
        size -= count;
        wr += count;
    }
    free(buffer);
}

extern "C" __attribute__((visibility("default")))
void load(int fd) {
    if (!nds) return;
    off_t pos = lseek(fd, 0, SEEK_END);
    if (pos < 0) { perror("lseek failed"); return; }
    lseek(fd, 0, SEEK_SET);
    uint8_t *buffer = (uint8_t*)malloc(pos);
    size_t count = pos;
    uint8_t *rd = buffer;
    while (count > 0) {
        ssize_t c = read(fd, rd, count);
        if (c < 0) { perror("read failed"); break; }
        rd += c;
        count -= c;
    }
    load_str(pos, buffer);
    free(buffer);
}

extern "C" __attribute__((visibility("default")))
void dump_state(const char* filename) {
    if (!nds) return;
    int fd = open(filename, O_CREAT | O_TRUNC | O_WRONLY, 0700);
    if (fd == -1) { perror("failed to open"); return; }
    save(fd);
    close(fd);
}

extern "C" __attribute__((visibility("default")))
void load_state(const char* filename) {
    if (!nds) return;
    int fd = open(filename, O_RDONLY, 0700);
    if (fd == -1) { perror("failed to open"); return; }
    load(fd);
    close(fd);
}

#endif // wasm32
