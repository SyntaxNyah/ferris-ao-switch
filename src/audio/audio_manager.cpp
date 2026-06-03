#include "audio_manager.hpp"
#include "../assets/asset_manager.hpp"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <SDL2/SDL.h>
#include <opus/opusfile.h>   // op_open_memory / op_read_stereo — Opus SFX decode

namespace ao {

// ── Opus SFX decode (the blip/SFX "no sound" fix) ───────────────────────────────
//
// SDL2_mixer 2.0.4 (the devkitPro portlib) detects an Ogg-framed file purely by
// its "OggS" magic and ALWAYS classifies it as MUS_OGG → it hands the bytes to
// the Vorbis decoder. An Opus file also starts with "OggS" but is NOT Vorbis, so
// the decode fails and the chunk loads as null → the sound is silently dropped.
// `Mix_LoadWAV_RW` routes compressed audio through that same broken detection, so
// every Opus blip/SFX (and AO content is overwhelmingly Opus) plays nothing. The
// music path dodges this by forcing the type via Mix_LoadMUSType_RW, but there is
// no typed chunk loader, so we decode Opus ourselves with libopusfile (already
// linked) and wrap the PCM in a WAV that Mix_LoadWAV_RW ingests natively.
//
// libopusfile always decodes to 48 kHz; op_read_stereo forces 2-channel S16
// interleaved output — exactly the device App::init opens — so the WAV we build
// is in the device format and Mix only has to copy it.
namespace {

void wr_le16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void wr_le32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

// Write a canonical 44-byte PCM WAV header into `h`.
void write_wav_header(uint8_t* h, uint32_t rate, uint16_t ch,
                      uint16_t bits, uint32_t data_bytes) {
    const uint16_t block     = (uint16_t)(ch * (bits / 8));
    const uint32_t byte_rate = rate * block;
    std::memcpy(h + 0,  "RIFF", 4);
    wr_le32(h + 4, 36 + data_bytes);
    std::memcpy(h + 8,  "WAVE", 4);
    std::memcpy(h + 12, "fmt ", 4);
    wr_le32(h + 16, 16);          // PCM fmt chunk size
    wr_le16(h + 20, 1);           // audio format = PCM
    wr_le16(h + 22, ch);
    wr_le32(h + 24, rate);
    wr_le32(h + 28, byte_rate);
    wr_le16(h + 32, block);
    wr_le16(h + 34, bits);
    std::memcpy(h + 36, "data", 4);
    wr_le32(h + 40, data_bytes);
}

// True if the bytes are an Opus stream ("OggS" container carrying "OpusHead").
// This is the exact check SDL2_mixer 2.0.4's detector is missing.
bool looks_like_opus(const uint8_t* d, int n) {
    return n >= 36 && std::memcmp(d, "OggS", 4) == 0 &&
           std::memcmp(d + 28, "OpusHead", 8) == 0;
}

// Decode an in-memory Opus file to a Mix_Chunk (48 kHz / S16 / stereo via WAV).
// Returns nullptr on any error.
Mix_Chunk* opus_bytes_to_chunk(const uint8_t* data, int size) {
    int err = 0;
    OggOpusFile* of = op_open_memory(data, (size_t)size, &err);
    if (!of) return nullptr;

    constexpr int      SR = 48000, CH = 2, FULL_PACKET = 5760 * CH; // 120 ms stereo
    size_t      cap = 0, len = 0;            // counts of interleaved int16 samples
    opus_int16* pcm = nullptr;

    // Pre-size from the known total when available (seekable memory stream → it is).
    const ogg_int64_t total = op_pcm_total(of, -1);   // samples per channel
    if (total > 0) {
        cap = (size_t)total * CH;
        pcm = (opus_int16*)SDL_malloc(cap * sizeof(opus_int16));
    }

    opus_int16 frame[FULL_PACKET];
    bool ok = true;
    for (;;) {
        const int n = op_read_stereo(of, frame, FULL_PACKET);  // samples per channel
        if (n < 0) { ok = false; break; }                      // decode error
        if (n == 0) break;                                     // end of stream
        const size_t add = (size_t)n * CH;
        if (len + add > cap) {
            size_t ncap = cap ? cap * 2 : (add * 8);
            while (ncap < len + add) ncap *= 2;
            opus_int16* np = (opus_int16*)SDL_realloc(pcm, ncap * sizeof(opus_int16));
            if (!np) { ok = false; break; }
            pcm = np; cap = ncap;
        }
        std::memcpy(pcm + len, frame, add * sizeof(opus_int16));
        len += add;
    }
    op_free(of);
    if (!ok || !pcm || len == 0) { SDL_free(pcm); return nullptr; }

    const uint32_t data_bytes = (uint32_t)(len * sizeof(opus_int16));
    uint8_t* wav = (uint8_t*)SDL_malloc(44 + data_bytes);
    if (!wav) { SDL_free(pcm); return nullptr; }
    write_wav_header(wav, SR, CH, 16, data_bytes);
    std::memcpy(wav + 44, pcm, data_bytes);
    SDL_free(pcm);

    SDL_RWops* rw = SDL_RWFromConstMem(wav, (int)(44 + data_bytes));
    // freesrc=1 frees the RWops struct (not the const mem) and converts to the
    // mixer's device format internally; the chunk owns its PCM afterwards.
    Mix_Chunk* c = rw ? Mix_LoadWAV_RW(rw, 1) : nullptr;
    SDL_free(wav);
    return c;
}

// Load a cached/local SFX file into a Mix_Chunk, decoding Opus via opusfile and
// everything else (WAV / Vorbis-OGG / MP3 / FLAC) via Mix_LoadWAV_RW. Never hits
// the network: the bytes come from the prefetch cache or sdmc:/romfs: only.
Mix_Chunk* load_sfx_chunk(const char* path) {
    SDL_RWops* rw = AssetManager::open_rwops_cached(path);   // no HTTP
    if (!rw) return nullptr;                                 // not ready — skip silently

    const Sint64 sz = SDL_RWsize(rw);
    if (sz <= 0) { SDL_RWclose(rw); return nullptr; }

    uint8_t* bytes = (uint8_t*)SDL_malloc((size_t)sz);
    if (!bytes) { SDL_RWclose(rw); return nullptr; }
    const Sint64 rd = SDL_RWread(rw, bytes, 1, (size_t)sz);
    SDL_RWclose(rw);
    if (rd != sz) { SDL_free(bytes); return nullptr; }

    Mix_Chunk* c = looks_like_opus(bytes, (int)sz)
        ? opus_bytes_to_chunk(bytes, (int)sz)
        : Mix_LoadWAV_RW(SDL_RWFromConstMem(bytes, (int)sz), 1);
    SDL_free(bytes);
    return c;
}

} // namespace

AudioManager::~AudioManager() {
    shutdown();
}

void AudioManager::shutdown() {
    stop_sfx();
    for (auto& s : sfx_cache_)
        if (s.chunk) { Mix_FreeChunk(s.chunk); s.chunk = nullptr; }
}

bool AudioManager::init() {
    Mix_AllocateChannels(SFX_CHANNELS);
    return true;
}

int AudioManager::find_sfx(const char* path) const {
    for (int i = 0; i < SFX_CACHE_SLOTS; ++i)
        if (sfx_cache_[i].chunk && std::strcmp(sfx_cache_[i].path, path) == 0)
            return i;
    return -1;
}

int AudioManager::evict_sfx() const {
    for (int i = 0; i < SFX_CACHE_SLOTS; ++i)
        if (!sfx_cache_[i].chunk) return i;
    int oldest = 0;
    for (int i = 1; i < SFX_CACHE_SLOTS; ++i)
        if (sfx_cache_[i].last_used < sfx_cache_[oldest].last_used)
            oldest = i;
    return oldest;
}

// `path` is a RELATIVE asset path (e.g. "sounds/sfx-guilty.opus").
// NON-BLOCKING: a cached chunk plays instantly; otherwise the bytes are only
// taken from the prefetch cache or local files (NEVER the network), so this can
// be called every typewriter blip without ever stalling the render loop. The
// courtroom prefetches blip/SFX bytes ahead of time so they're ready here.
bool AudioManager::play_sfx(const char* path) {
    int idx = find_sfx(path);
    if (idx < 0) {
        Mix_Chunk* c = load_sfx_chunk(path);
        if (!c) return false;   // not ready yet, or undecodable — skip (no freeze)
        idx = evict_sfx();
        if (sfx_cache_[idx].chunk) Mix_FreeChunk(sfx_cache_[idx].chunk);
        sfx_cache_[idx].chunk = c;
        std::strncpy(sfx_cache_[idx].path, path,
            sizeof(sfx_cache_[idx].path) - 1);
    }
    sfx_cache_[idx].last_used = SDL_GetTicks();
    return Mix_PlayChannel(-1, sfx_cache_[idx].chunk, 0) >= 0;
}

void AudioManager::stop_sfx() {
    Mix_HaltChannel(-1);
}

void AudioManager::set_sfx_volume(int vol) {
    Mix_Volume(-1, vol);
}

} // namespace ao
