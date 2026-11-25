// Bus round-trip test: seqlock integrity, timestamp filter, bus separation.
#include "dsp/Bus.hpp"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <unistd.h>

using namespace qdssd;

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok) ++failures;
}

int main()
{
    ::unlink (kBusPath);
    BusMapping writer;
    check (writer.open (true), "writer opens the shared-memory pool");

    BusMapping reader;
    check (reader.open (false), "reader opens the same pool");

    BusFrame f;
    f.flags = 1;
    f.timeNs = monotonicNs();
    f.level = 0.25f;
    f.chroma[3] = 200;
    f.notes[69] = 255;
    f.spectrum[10] = -12;
    writeFrame (writer.data(), 0, f);

    BusFrame out;
    check (readFrame (reader.data(), 0, monotonicNs(), out), "frame is readable");
    check (out.notes[69] == 255 && out.chroma[3] == 200 && out.spectrum[10] == -12,
           "payload survives the round trip");
    check (std::fabs (out.level - 0.25f) < 1e-6f, "level survives");

    // Bus separation.
    BusFrame other;
    other.timeNs = f.timeNs;
    other.notes[60] = 42;
    writeFrame (writer.data(), 1, other);
    BusFrame got;
    readFrame (reader.data(), 1, monotonicNs(), got);
    check (got.notes[60] == 42 && got.notes[69] == 0, "buses are independent");

    // Timestamp filter: a frame newer than notAfter must be ignored.
    BusFrame future;
    future.timeNs = f.timeNs + 5000000000ull;
    future.notes[70] = 7;
    writeFrame (writer.data(), 2, future);
    check (! readFrame (reader.data(), 2, monotonicNs(), got), "future frames are ignored");

    // Many writers/readers stay consistent (seqlock).
    bool consistent = true;
    for (int i = 0; i < 1000; ++i)
    {
        BusFrame a;
        a.timeNs = monotonicNs() + (uint64_t) i;
        a.level = (float) i;
        std::memset (a.notes, (int) (i & 0x7f), sizeof a.notes);
        writeFrame (writer.data(), 3, a);
        BusFrame b;
        if (readFrame (reader.data(), 3, a.timeNs, b))
        {
            if (b.level != (float) i)
                consistent = false;
        }
    }
    check (consistent, "1000 frames round-trip");

    ::unlink (kBusPath);
    std::printf ("\n%s (%d failures)\n", failures == 0 ? "bus-test: PASS" : "bus-test: FAIL", failures);
    return failures == 0 ? 0 : 1;
}
