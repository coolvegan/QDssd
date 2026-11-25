// Lock-free shared-memory bus between QDSSD SC Input (writer) and QDSSD (reader).
//
// Eight buses, each a ring of analysis frames. One frame per STFT hop. A
// seqlock protects every slot; readers retry instead of blocking. Frames carry
// a monotonic timestamp so the reader can align the key with its own latency.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace qdssd
{

struct BusFrame
{
    uint32_t seq = 0;
    uint32_t flags = 0;      // 1 = analysis valid, 2 = silent, 4 = clipped
    uint64_t timeNs = 0;     // CLOCK_MONOTONIC at frame start
    float level = 0.0f;      // key RMS, linear
    uint8_t chroma[12] = {};
    uint8_t notes[128] = {};
    int8_t spectrum[128] = {};
    uint8_t pad[40] = {};
};

constexpr int kBusCount = 8;
constexpr int kBusRing = 64;
constexpr const char* kBusPath = "/dev/shm/qdssd_sc_buses_v1";
constexpr uint32_t kBusMagic = 0x51445353u;   // "QDSS"
constexpr uint32_t kBusVersion = 1u;

struct BusPool
{
    uint32_t magic = 0;
    uint32_t version = 0;
    BusFrame frames[kBusCount][kBusRing];
};

inline uint64_t monotonicNs()
{
    timespec ts {};
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
}

class BusMapping
{
public:
    ~BusMapping() { close(); }

    bool open (bool create)
    {
        if (pool != nullptr)
            return true;

        int fd = ::open (kBusPath, create ? (O_CREAT | O_RDWR) : O_RDWR, 0666);
        if (fd < 0 && create)
            fd = ::open (kBusPath, O_RDWR, 0666);
        if (fd < 0)
            return false;

        struct stat st {};
        if (::fstat (fd, &st) != 0 || (size_t) st.st_size < sizeof (BusPool))
        {
            if (! create || ::ftruncate (fd, (off_t) sizeof (BusPool)) != 0)
            {
                ::close (fd);
                return false;
            }
        }

        void* mem = ::mmap (nullptr, sizeof (BusPool), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close (fd);
        if (mem == MAP_FAILED)
            return false;

        pool = static_cast<BusPool*> (mem);
        if (create && (pool->magic != kBusMagic || pool->version != kBusVersion))
        {
            std::memset (pool, 0, sizeof (BusPool));
            pool->magic = kBusMagic;
            pool->version = kBusVersion;
        }
        if (pool->magic != kBusMagic || pool->version != kBusVersion)
        {
            close();
            return false;
        }
        return true;
    }

    void close()
    {
        if (pool != nullptr)
        {
            ::munmap (pool, sizeof (BusPool));
            pool = nullptr;
        }
    }

    BusPool* data() { return pool; }

private:
    BusPool* pool = nullptr;
};

inline void writeFrame (BusPool* pool, int bus, const BusFrame& src)
{
    if (pool == nullptr || bus < 0 || bus >= kBusCount)
        return;
    BusFrame& dst = pool->frames[bus][(uint32_t) (src.timeNs / 1000000ull) % kBusRing];
    const uint32_t s = dst.seq;
    __atomic_store_n (&dst.seq, (s | 1u), __ATOMIC_RELEASE);
    __atomic_thread_fence (__ATOMIC_RELEASE);
    dst.flags = src.flags;
    dst.timeNs = src.timeNs;
    dst.level = src.level;
    std::memcpy (dst.chroma, src.chroma, sizeof dst.chroma);
    std::memcpy (dst.notes, src.notes, sizeof dst.notes);
    std::memcpy (dst.spectrum, src.spectrum, sizeof dst.spectrum);
    __atomic_thread_fence (__ATOMIC_RELEASE);
    __atomic_store_n (&dst.seq, s + 2u, __ATOMIC_RELEASE);
}

// Returns the newest frame that is at least `notAfterNs` old (i.e. its timestamp
// is <= notAfterNs). Older frames are ignored.
inline bool readFrame (BusPool* pool, int bus, uint64_t notAfterNs, BusFrame& out)
{
    if (pool == nullptr || bus < 0 || bus >= kBusCount)
        return false;

    bool found = false;
    uint64_t best = 0;

    for (int i = 0; i < kBusRing; ++i)
    {
        const BusFrame& f = pool->frames[bus][i];
        const uint32_t s0 = __atomic_load_n (&f.seq, __ATOMIC_ACQUIRE);
        if (s0 & 1u)
            continue;
        if (f.timeNs == 0 || f.timeNs > notAfterNs || f.timeNs < best)
            continue;

        BusFrame copy;
        std::memcpy (&copy, &f, sizeof (BusFrame));
        const uint32_t s1 = __atomic_load_n (&f.seq, __ATOMIC_ACQUIRE);
        if (s0 != s1)
            continue;

        out = copy;
        best = copy.timeNs;
        found = true;
    }
    return found;
}

} // namespace qdssd
