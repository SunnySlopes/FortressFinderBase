//
// FortressFinderBase public engine API.
//
// This header is the contract shared by the standalone command-line program
// (fortressfinder-cli) and the JNI bridge (fortressFinderLibJ).  All Minecraft
// / cubiomes computation lives in fortress_finder.cpp and the nested cubiomes
// submodule; the Java GUI never compiles native code of its own.
//

#ifndef FORTRESSFINDER_FORTRESS_FINDER_H
#define FORTRESSFINDER_FORTRESS_FINDER_H

#include "Thread.h"

#include <atomic>
#include <cstdint>
#include <vector>

/** Live counters published by a running search. */
struct Progress {
    std::atomic_int current{0};
    std::atomic_int total{0};
    std::atomic_int chunkInRunning{0};
    std::atomic_int phase1{0};
    std::atomic_bool try_pause{false};
    std::atomic_bool try_stop{false};
};

/** One fortress result. Interpretation depends on SearchConfig::mode. */
struct FortressHit {
    int chunkX = 0;
    int chunkZ = 0;
    int outX = 0;
    int outY = 0;
    int outZ = 0;
    int mode = 0;
    int shapeCode = 0;
    int longEdge = 0;   // span mode: spanX
    int shortEdge = 0;  // span mode: spanZ
    int heightEdge = 0; // span mode: spanY (maxY - minY)

    bool operator<(const FortressHit &other) const noexcept
    {
        if (outX != other.outX)
            return outX < other.outX;
        return outZ < other.outZ;
    }
};

/**
 * Search parameters.  The Java GUI converts its rectangle into center/radius
 * exactly like FortressSearchRunner.rectToCenterAndRadius(); the CLI exposes
 * both forms.
 */
struct SearchConfig {
    int mode = 0; // 0 = cross, 1 = span
    int mc = 25;  // cubiomes MC_1_20
    uint64_t seed = 0;
    int centerX = 0;
    int centerZ = 0;
    int r = 0;
    int crossFilter = 2; // 2 = all, 3 = triple/quad, 4 = quad only
    int minLong = 0;
    int minShort = 0;
    int minHeight = 0;
};

/** Global pause / resume / stop control shared by all in-flight searches. */
void fortressControlPause();
void fortressControlResume();
void fortressControlStop();
void fortressControlReset();

/** True when a stop was requested by any caller. */
bool fortressStopRequested();

/**
 * Runs a fortress search.  `progress` may be null; `out` receives the hits.
 * The function is synchronous and thread-safe with respect to independent
 * searches, matching the historical JNI behaviour.
 */
void runFortressSearch(const SearchConfig &cfg,
                       Progress *progress,
                       ThreadSafeResults<FortressHit> &out,
                       int numThreads);

#endif // FORTRESSFINDER_FORTRESS_FINDER_H
