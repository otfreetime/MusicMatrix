#pragma once
#include <cstring>
#include <cstdint>
#include <atomic>

//==============================================================================
// Cross-process audio ring buffer via shared memory (output channel only).
//
// Memory layout (total = kAudioSharedMemTotalSize bytes):
//   [0..3]   std::atomic<int32_t> writePos  - absolute sample index written by worker
//   [4..7]   std::atomic<int32_t> readPos   - absolute sample index read by host
//   [8..11]  int32_t capacity           - ring buffer capacity (samples per channel)
//   [12..15] int32_t numChannels        - always 2
//   [16...]  float[2][capacity]         - planar: ch0 first, then ch1
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
inline void audioRingBufferInit (void* mem, int requestedCapacity = kAudioRingCapacity) noexcept
{
    int maxCapacity = (kAudioSharedMemTotalSize - kAudioSharedMemHeaderSize) 
                      / (kAudioSharedMemNumChannels * (int) sizeof (float));
    int actualCapacity = requestedCapacity < maxCapacity ? requestedCapacity : maxCapacity;

    auto* hdr = static_cast<std::atomic<int32_t>*> (mem);
    hdr[0].store(0, std::memory_order_release); // writePos
    hdr[1].store(0, std::memory_order_release); // readPos
    
    auto* rawHdr = static_cast<int32_t*> (mem);
    rawHdr[2] = actualCapacity;                 // capacity
    rawHdr[3] = kAudioSharedMemNumChannels;

    std::memset (static_cast<char*> (mem) + kAudioSharedMemHeaderSize,
                 0,
                 static_cast<size_t> (kAudioSharedMemTotalSize - kAudioSharedMemHeaderSize));
}

//==============================================================================
// Helper: Get capacities
inline int audioRingBufferGetCapacity(void* mem) noexcept
{
    return static_cast<int32_t*>(mem)[2];
}

//==============================================================================
// Returns the number of free samples available for writing (worker side).
inline int audioRingBufferGetFreeSpace (void* mem) noexcept
{
    auto* hdr = static_cast<std::atomic<int32_t>*> (mem);
    const int wp  = hdr[0].load(std::memory_order_acquire); // read
    const int rp  = hdr[1].load(std::memory_order_acquire);
    const int cap = static_cast<int32_t*>(mem)[2];

    int free = rp - wp - 1;
    if (free < 0)
        free += cap;
    return free;
}

//==============================================================================
inline void audioRingBufferRead (void* mem, float** destChannels, int numSamples) noexcept
{
    auto* hdr = static_cast<std::atomic<int32_t>*> (mem);
    const int wp  = hdr[0].load(std::memory_order_acquire); // Load write pos FIRST (load-acquire).
    int rp        = hdr[1].load(std::memory_order_relaxed);
    
    auto* rawHdr = static_cast<int32_t*>(mem);
    const int cap = rawHdr[2];
    const int ch  = rawHdr[3];

    int avail = wp - rp;
    if (avail < 0)
        avail += cap;

    const int toRead = (avail < numSamples) ? avail : numSamples;

    const auto* audio = reinterpret_cast<const float*> (
        static_cast<const char*> (mem) + kAudioSharedMemHeaderSize);

    for (int i = 0; i < toRead; ++i)
    {
        const int pos = (rp + i) % cap;
        for (int ch = 0; ch < kAudioSharedMemNumChannels; ++ch)
            destChannels[ch][i] = audio[ch * cap + pos];
    }

    // Zero-fill any underrun samples.
    if (toRead < numSamples)
    {
        for (int ch = 0; ch < kAudioSharedMemNumChannels; ++ch)
            std::memset (destChannels[ch] + toRead, 0, static_cast<size_t> ((numSamples - toRead) * sizeof (float)));
    }

    hdr[1].store((rp + toRead) % cap, std::memory_order_release); // Publish read position AFTER reading.
}

//==============================================================================
inline void audioRingBufferWrite (void* mem, const float** srcChannels, int numSamples) noexcept
{
    auto* hdr = static_cast<std::atomic<int32_t>*> (mem);
    int wp        = hdr[0].load(std::memory_order_relaxed);
    const int rp  = hdr[1].load(std::memory_order_acquire); // Load read pos FIRST.

    auto* rawHdr = static_cast<int32_t*>(mem);
    const int cap = rawHdr[2];
    const int ch  = rawHdr[3];

    int free = rp - wp - 1;
    if (free < 0)
        free += cap;

    const int toWrite = (free < numSamples) ? free : numSamples;

    auto* audio   = reinterpret_cast<float*> (static_cast<char*> (mem) + kAudioSharedMemHeaderSize);

    for (int i = 0; i < toWrite; ++i)
    {
        const int pos = (wp + i) % cap;
        for (int ch = 0; ch < kAudioSharedMemNumChannels; ++ch)
            audio[ch * cap + pos] = srcChannels[ch][i];
    }

    // Publish write position AFTER samples are written (store-release).
    hdr[0].store((wp + toWrite) % cap, std::memory_order_release);
}
} // namespace myapp::bridge
