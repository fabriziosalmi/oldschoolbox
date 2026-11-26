#pragma once
#include <cstdint>

// Euclidean rhythm generator. Uses the Bresenham/fractional algorithm which
// produces patterns indistinguishable from true Bjorklund for all musically
// useful (hits, steps) combinations and is dramatically simpler.
//
// Returns a uint64_t bitmask of step-N pulses for given (hits, steps, rotate).
namespace Euclidean
{
    inline uint64_t generate(int hits, int steps, int rotate)
    {
        if (steps <= 0) return 0;
        if (steps > 64) steps = 64;
        if (hits <= 0)  return 0;
        if (hits >= steps)
            return (steps == 64) ? ~uint64_t(0) : ((1ULL << steps) - 1ULL);

        uint64_t pat = 0;
        for (int i = 0; i < hits; ++i)
        {
            // Bresenham distribution: i-th hit at floor((i * steps + hits/2) / hits)
            const int pos = (i * steps + hits / 2) / hits;
            const int finalPos = ((pos + rotate) % steps + steps) % steps;
            pat |= (1ULL << finalPos);
        }
        return pat;
    }
}
