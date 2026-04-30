#pragma once
#include <cstring>
#include <cstdint>

//==============================================================================
// Cross-process audio ring buffer via shared memory (output channel only).
//
// Memory layout (total = kAudioSharedMemTotalSize bytes):
//   [0..3]   volatile int32_t writePos  - absolute sample index written by worker
//   [4..7]   volatile int32_t readPos   - absolute sample index read by host
//   [8..11]  int32_t capacity           - ring buffer capacity (samples per channel)
//   [12..15] int32_t numChannels        - always 2
//   [16...]  float[2][capacity]         - planar: ch0 first, then ch1
//
// Both writePos and readPos are kept in [0, capacity) via modulo.
// x86 guarantees naturally-atomic 4-byte aligned reads/writes, which is
// sufficient here (we only need acquire/release semantics, not CAS).
//==============================================================================

namespace myapp::bridge
{

static constexpr int kAudioSharedMemTotalSize  = 65536;
static constexpr int kAudioSharedMemHeaderSize = 16;
static constexpr int kAudioSharedMemNumChannels = 2;

// Number of samples per channel that fit in the ring buffer.
// (65536 - 16) / (2 channels * 4 bytes/sample) = 8190 samples ≈ 185 ms @ 44100
static constexpr int kAudioRingCapacity =
    (kAudioSharedMemTotalSize - kAudioSharedMemHeaderSize)
    / (kAudioSharedMemNumChannels * (int) sizeof (float));

//==============================================================================
// Called once after the shared memory file is created/mapped.
inline void audioRingBufferInit (void* mem) noexcept
{
    auto* hdr = static_cast<volatile int32_t*> (mem);
    hdr[0] = 0;                         // writePos
    hdr[1] = 0;                         // readPos
    hdr[2] = kAudioRingCapacity;        // capacity
    hdr[3] = kAudioSharedMemNumChannels;

    std::memset (static_cast<char*> (mem) + kAudioSharedMemHeaderSize,
                 0,
                 static_cast<size_t> (kAudioSharedMemTotalSize - kAudioSharedMemHeaderSize));
}

//==============================================================================
// Worker side: push one block of planar float audio into the ring buffer.
// channelData[ch] must have numSamples elements.
inline void audioRingBufferWrite (void* mem, const float* const* channelData, int numSamples) noexcept
{
    auto* hdr     = static_cast<volatile int32_t*> (mem);
    const int cap = hdr[2];
    const int wp  = hdr[0];

    auto* audio   = reinterpret_cast<float*> (static_cast<char*> (mem) + kAudioSharedMemHeaderSize);

    for (int i = 0; i < numSamples; ++i)
    {
        const int pos = (wp + i) % cap;
        for (int ch = 0; ch < kAudioSharedMemNumChannels; ++ch)
            audio[ch * cap + pos] = channelData[ch][i];
    }

    // Publish write position AFTER samples are written (store-release on x86).
    hdr[0] = (wp + numSamples) % cap;
}

//==============================================================================
// Returns the number of free samples available for writing (worker side).
inline int audioRingBufferFreeSpace (const void* mem) noexcept
{
    auto* hdr     = static_cast<const volatile int32_t*> (mem);
    const int cap = hdr[2];
    const int wp  = hdr[0];
    const int rp  = hdr[1];
    const int used = (wp - rp + cap) % cap;
    return cap - used - 1; // -1 to distinguish full from empty
}

// Host side: pull numSamples from the ring buffer into planar channelData.
// Returns the number of samples actually read (may be < numSamples on underrun).
// Any unfilled portion is zeroed.
inline int audioRingBufferRead (const void* mem, float** channelData, int numSamples) noexcept
{
    auto* hdr     = static_cast<const volatile int32_t*> (mem);
    auto* hdrRW   = const_cast<volatile int32_t*> (hdr);
    const int cap = hdr[2];

    // Load write pos FIRST (load-acquire on x86).
    const int wp  = hdr[0];
    const int rp  = hdr[1];

    const int available = (wp - rp + cap) % cap;
    const int toRead    = (available < numSamples) ? available : numSamples;

    const auto* audio = reinterpret_cast<const float*> (
        static_cast<const char*> (mem) + kAudioSharedMemHeaderSize);

    for (int i = 0; i < toRead; ++i)
    {
        const int pos = (rp + i) % cap;
        for (int ch = 0; ch < kAudioSharedMemNumChannels; ++ch)
            channelData[ch][i] = audio[ch * cap + pos];
    }

    // Zero-fill any underrun samples.
    if (toRead < numSamples)
    {
        for (int ch = 0; ch < kAudioSharedMemNumChannels; ++ch)
            std::memset (channelData[ch] + toRead, 0, static_cast<size_t> ((numSamples - toRead) * sizeof (float)));
    }

    // Publish updated read pos AFTER reading.
    hdrRW[1] = (rp + toRead) % cap;

    return toRead;
}

} // namespace myapp::bridge
