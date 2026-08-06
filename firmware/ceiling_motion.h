// ceiling_motion.h
// -----------------------------------------------------------------------------
// Pure, dependency-free motion + layout planning core for the WLED "Ceiling
// Mover" project.
//
// This header contains NO WLED includes and NO Arduino types on purpose:
//   * The WLED usermod (usermod_ceiling_mover.h) includes it to plan and drive
//     the moving-block animation.
//   * The host test harness (test/test_motion.cpp) includes the SAME file and
//     verifies every one-block <-> two-block transition on a normal PC with g++.
//
// So the code that is unit-tested is byte-for-byte the code that ships in the
// firmware. Everything here is integer/float math on pixel indices; colour,
// brightness and RGBW handling live entirely in the usermod (see DESIGN.md,
// stage 5) so that this layer can never accidentally alter a colour.
//
// Coordinate model
// ----------------
//   * The logical strip is a continuous range of pixels [0 .. stripLen-1]
//     (864 pixels for this installation). The four physical outputs are mapped
//     to this range by the WLED bus manager, not here (see INSTALL.md stage 4).
//   * A Block is an inclusive pixel span [start, end]. A Layout has 1 or 2
//     blocks.
//   * A MovingBlock stores FLOAT source and destination spans. Blocks may live
//     partly or fully off-screen (negative, or >= stripLen) so that "enter from
//     edge" / "exit toward edge" animations are just ordinary moves that get
//     clipped at render time.
// -----------------------------------------------------------------------------
#ifndef CEILING_MOTION_H
#define CEILING_MOTION_H

#include <math.h>
#include <stddef.h>

namespace ceiling {

// ------------------------------- constants ----------------------------------
static const int   MAX_BLOCKS = 2;      // a layout / transition uses up to 2 blocks
static const float PI_F       = 3.14159265358979323846f;

// ------------------------------- data model ---------------------------------

// Inclusive integer pixel span. Empty when start > end.
struct Block {
    int start;
    int end;
    int width() const { return (end >= start) ? (end - start + 1) : 0; }
    int center() const { return (start + end) / 2; }
};

// A predefined lighting layout: one or two illuminated blocks.
struct Layout {
    int   count;                 // 0, 1 or 2 (0 == "off": nothing lit)
    Block blocks[MAX_BLOCKS];
};

// A single block in flight, from a float source span to a float dest span.
// Position and width are interpolated together, so a block can simultaneously
// translate AND resize (needed for split / merge). "colorIndex" tells the
// usermod which captured source colour to paint this block with.
struct MovingBlock {
    float srcStart, srcEnd;      // may be < 0 or >= stripLen (off-screen)
    float dstStart, dstEnd;
    int   colorIndex;            // index into the usermod's captured colour set
};

struct TransitionPlan {
    int         count;                   // number of moving blocks (1 or 2)
    MovingBlock blocks[MAX_BLOCKS];
};

// -------------------------- motion / ON-OFF modes ---------------------------
enum EdgeMode {
    EDGE_LEFT = 0,   // enter from / exit toward the left edge (pixel 0)
    EDGE_RIGHT,      // enter from / exit toward the right edge (pixel stripLen-1)
    EDGE_BOTH,       // both edges (split / converge)
    EDGE_NEAREST,    // whichever edge is closest to each block
    EDGE_NONE,       // no animation (instant)
    EDGE_RANDOM      // caller resolves to one of the above before planning
};

// Cosine-eased trapezoidal velocity profile. Average-speed / peak-speed ratio
// of this shape equals SHAPE_AREA below, which lets us solve the duration so
// the fastest block cruises at exactly maxSpeed.
struct MotionParams {
    float maxSpeed;     // pixels per second at cruise (the configurable cap)
    float accelFrac;    // fraction of the move spent accelerating   (~0.4)
    float cruiseFrac;   // fraction spent at full speed               (~0.2)
    float decelFrac;    // fraction spent decelerating                (~0.4)
    float minDuration;  // seconds, lower clamp (avoids 0-length moves)
    float maxDuration;  // seconds, safety upper clamp
    int   fps;          // target render rate (50-55 for this install)

    // Sensible defaults matching the previously preferred feel
    // (accel 0.8 / decel 0.8 / cruise 0.2  ->  0.4 / 0.2 / 0.4 phase split).
    static MotionParams defaults() {
        MotionParams p;
        p.maxSpeed    = 700.0f;   // px/s; tune to taste
        p.accelFrac   = 0.4f;
        p.cruiseFrac  = 0.2f;
        p.decelFrac   = 0.4f;
        p.minDuration = 0.25f;
        p.maxDuration = 8.0f;
        p.fps         = 52;
        return p;
    }
};

// --------------------------- kinematics helpers -----------------------------

// Normalise the three phase fractions so they sum to 1 (defensive).
inline void normFractions(const MotionParams& p, float& fa, float& fc, float& fd) {
    fa = p.accelFrac; fc = p.cruiseFrac; fd = p.decelFrac;
    float s = fa + fc + fd;
    if (s <= 0.0f) { fa = 0.4f; fc = 0.2f; fd = 0.4f; s = 1.0f; }
    fa /= s; fc /= s; fd /= s;
}

// Ratio of average speed to peak (cruise) speed for the eased trapezoid.
// The eased accel/decel ramps each have average height 0.5, so the area under
// the normalised velocity curve is 0.5*fa + fc + 0.5*fd.
inline float shapeArea(const MotionParams& p) {
    float fa, fc, fd; normFractions(p, fa, fc, fd);
    return 0.5f * fa + fc + 0.5f * fd;
}

// Total animation duration (seconds) for a move whose largest block travels
// |maxDistance| pixels, so that block cruises at exactly maxSpeed.
inline float planDuration(float maxDistance, const MotionParams& p) {
    float d = maxDistance < 0 ? -maxDistance : maxDistance;
    if (d < 1e-4f) return 0.0f;                       // nothing moves -> instant
    float A = shapeArea(p);
    float speed = (p.maxSpeed > 1e-3f) ? p.maxSpeed : 1.0f;
    float T = d / (speed * A);
    if (T < p.minDuration) T = p.minDuration;
    if (T > p.maxDuration) T = p.maxDuration;
    return T;
}

// Eased progress s in [0,1] for elapsed time t within total duration T.
// s(0)=0, s(T)=1, and the velocity (ds/dt) is 0 at both ends (rest to rest)
// with a constant-velocity cruise in the middle. Monotonic non-decreasing.
inline float easedProgress(float t, float T, const MotionParams& p) {
    if (T <= 1e-6f) return 1.0f;
    float u = t / T;
    if (u <= 0.0f) return 0.0f;
    if (u >= 1.0f) return 1.0f;

    float fa, fc, fd; normFractions(p, fa, fc, fd);
    float A = 0.5f * fa + fc + 0.5f * fd;   // == shapeArea
    float I;                                // cumulative area (un-normalised)

    if (u <= fa) {
        // accel: v = (1 - cos(pi*u/fa)) / 2
        I = 0.5f * (u - (fa / PI_F) * sinf(PI_F * u / fa));
    } else if (u <= fa + fc) {
        // cruise: v = 1
        I = 0.5f * fa + (u - fa);
    } else {
        // decel: v = (1 + cos(pi*v/fd)) / 2, v measured from cruise end
        float v = u - fa - fc;
        I = 0.5f * fa + fc + 0.5f * (v + (fd / PI_F) * sinf(PI_F * v / fd));
    }
    float s = I / A;
    if (s < 0.0f) s = 0.0f;
    if (s > 1.0f) s = 1.0f;
    return s;
}

// -------------------------- geometry / rendering ----------------------------

// Interpolate a moving block's current float span at progress s in [0,1].
inline void blockSpanAt(const MovingBlock& b, float s, float& outStart, float& outEnd) {
    outStart = b.srcStart + (b.dstStart - b.srcStart) * s;
    outEnd   = b.srcEnd   + (b.dstEnd   - b.srcEnd)   * s;
}

// Convert a float span to inclusive integer pixel indices, clipped to the strip.
// Returns false if nothing of the block is on-screen. Rounds to nearest.
inline bool spanToPixels(float fStart, float fEnd, int stripLen, int& iStart, int& iEnd) {
    if (fEnd < fStart) { float t = fStart; fStart = fEnd; fEnd = t; }
    int a = (int)lroundf(fStart);
    int b = (int)lroundf(fEnd);
    if (b < 0 || a > stripLen - 1) return false;      // fully off-screen
    if (a < 0) a = 0;
    if (b > stripLen - 1) b = stripLen - 1;
    if (b < a) return false;
    iStart = a; iEnd = b;
    return true;
}

// Largest absolute distance any endpoint of the plan travels; drives duration.
inline float planMaxDistance(const TransitionPlan& plan) {
    float m = 0.0f;
    for (int i = 0; i < plan.count; ++i) {
        const MovingBlock& b = plan.blocks[i];
        float ds = b.dstStart - b.srcStart; if (ds < 0) ds = -ds;
        float de = b.dstEnd   - b.srcEnd;   if (de < 0) de = -de;
        if (ds > m) m = ds;
        if (de > m) m = de;
    }
    return m;
}

// ----------------------------- plan builders --------------------------------
// Order blocks left-to-right so pairing is deterministic ("most logical path").
inline void sortedBlocks(const Layout& L, Block out[MAX_BLOCKS], int& n) {
    n = L.count;
    for (int i = 0; i < n; ++i) out[i] = L.blocks[i];
    if (n == 2 && out[0].start > out[1].start) {
        Block t = out[0]; out[0] = out[1]; out[1] = t;
    }
}

// Plan a layout -> layout transition (both have 1 or 2 blocks).
// colorIndex references the SOURCE block a moving block inherits its colour
// from (so split children share their parent's colour).
inline TransitionPlan planTransition(const Layout& src, const Layout& dst) {
    TransitionPlan plan; plan.count = 0;
    Block s[MAX_BLOCKS], d[MAX_BLOCKS]; int ns, nd;
    sortedBlocks(src, s, ns);
    sortedBlocks(dst, d, nd);

    if (ns == 1 && nd == 1) {
        MovingBlock b;
        b.srcStart = s[0].start; b.srcEnd = s[0].end;
        b.dstStart = d[0].start; b.dstEnd = d[0].end;
        b.colorIndex = 0;
        plan.blocks[plan.count++] = b;

    } else if (ns == 1 && nd == 2) {
        // Split the single source block into two sub-ranges sized in proportion
        // to the two destination block widths, then send each to its dest.
        float w0 = (float)d[0].width();
        float w1 = (float)d[1].width();
        float wsum = (w0 + w1 > 0) ? (w0 + w1) : 1.0f;
        float srcLen = (float)s[0].width();
        float split  = srcLen * (w0 / wsum);        // left child's length
        float srcMid = s[0].start + split;          // boundary inside source

        MovingBlock b0;
        b0.srcStart = s[0].start; b0.srcEnd = srcMid - 1.0f;
        b0.dstStart = d[0].start; b0.dstEnd = d[0].end;
        b0.colorIndex = 0;
        MovingBlock b1;
        b1.srcStart = srcMid;     b1.srcEnd = s[0].end;
        b1.dstStart = d[1].start; b1.dstEnd = d[1].end;
        b1.colorIndex = 0;
        plan.blocks[plan.count++] = b0;
        plan.blocks[plan.count++] = b1;

    } else if (ns == 2 && nd == 1) {
        // Two source blocks converge into one destination. Partition the dest
        // into two sub-ranges proportional to the source widths; left src ->
        // left part, right src -> right part.
        float w0 = (float)s[0].width();
        float w1 = (float)s[1].width();
        float wsum = (w0 + w1 > 0) ? (w0 + w1) : 1.0f;
        float dstLen = (float)d[0].width();
        float part   = dstLen * (w0 / wsum);
        float dstMid = d[0].start + part;

        MovingBlock b0;
        b0.srcStart = s[0].start; b0.srcEnd = s[0].end;
        b0.dstStart = d[0].start; b0.dstEnd = dstMid - 1.0f;
        b0.colorIndex = 0;
        MovingBlock b1;
        b1.srcStart = s[1].start; b1.srcEnd = s[1].end;
        b1.dstStart = dstMid;     b1.dstEnd = d[0].end;
        b1.colorIndex = 1;
        plan.blocks[plan.count++] = b0;
        plan.blocks[plan.count++] = b1;

    } else { // ns == 2 && nd == 2  -> pair by sorted position (min crossing)
        for (int i = 0; i < 2; ++i) {
            MovingBlock b;
            b.srcStart = s[i].start; b.srcEnd = s[i].end;
            b.dstStart = d[i].start; b.dstEnd = d[i].end;
            b.colorIndex = i;
            plan.blocks[plan.count++] = b;
        }
    }
    return plan;
}

// Resolve EDGE_NEAREST for one block given its center; returns EDGE_LEFT/RIGHT.
inline EdgeMode nearestEdge(int center, int stripLen) {
    int distL = center;                  // to pixel 0
    int distR = (stripLen - 1) - center; // to last pixel
    return (distL <= distR) ? EDGE_LEFT : EDGE_RIGHT;
}

// Build a same-width off-screen source span just beyond an edge for "enter".
inline void offscreenSource(EdgeMode edge, int width, int stripLen,
                            float& s0, float& s1) {
    if (edge == EDGE_LEFT) { s1 = -1.0f;               s0 = s1 - (width - 1); }
    else                   { s0 = (float)stripLen;      s1 = s0 + (width - 1); }
}

// Build a same-width off-screen dest span just beyond an edge for "exit".
inline void offscreenDest(EdgeMode edge, int width, int stripLen,
                          float& d0, float& d1) {
    if (edge == EDGE_LEFT) { d1 = -1.0f;               d0 = d1 - (width - 1); }
    else                   { d0 = (float)stripLen;      d1 = d0 + (width - 1); }
}

// ON: blocks slide in from off-screen to the destination layout.
inline TransitionPlan planOn(const Layout& dst, EdgeMode edge, int stripLen) {
    TransitionPlan plan; plan.count = 0;
    Block d[MAX_BLOCKS]; int nd; sortedBlocks(dst, d, nd);

    if (edge == EDGE_BOTH && nd == 1) {
        // single dest, enter as two halves from both edges, converging
        int w  = d[0].width();
        int wl = w / 2;
        Block dl = { d[0].start,        d[0].start + wl - 1 };
        Block dr = { d[0].start + wl,   d[0].end };
        MovingBlock b0, b1;
        offscreenSource(EDGE_LEFT,  dl.width(), stripLen, b0.srcStart, b0.srcEnd);
        b0.dstStart = dl.start; b0.dstEnd = dl.end; b0.colorIndex = 0;
        offscreenSource(EDGE_RIGHT, dr.width(), stripLen, b1.srcStart, b1.srcEnd);
        b1.dstStart = dr.start; b1.dstEnd = dr.end; b1.colorIndex = 0;
        plan.blocks[plan.count++] = b0;
        plan.blocks[plan.count++] = b1;
        return plan;
    }

    for (int i = 0; i < nd; ++i) {
        EdgeMode e = edge;
        if (edge == EDGE_NEAREST) e = nearestEdge(d[i].center(), stripLen);
        else if (edge == EDGE_BOTH) e = (i == 0) ? EDGE_LEFT : EDGE_RIGHT;
        MovingBlock b;
        offscreenSource(e, d[i].width(), stripLen, b.srcStart, b.srcEnd);
        b.dstStart = d[i].start; b.dstEnd = d[i].end; b.colorIndex = i;
        plan.blocks[plan.count++] = b;
    }
    return plan;
}

// OFF: current layout slides off-screen toward an edge; settled state = off.
inline TransitionPlan planOff(const Layout& src, EdgeMode edge, int stripLen) {
    TransitionPlan plan; plan.count = 0;
    Block s[MAX_BLOCKS]; int ns; sortedBlocks(src, s, ns);

    if (edge == EDGE_BOTH && ns == 1) {
        // single source, exit as two halves toward both edges
        int w  = s[0].width();
        int wl = w / 2;
        Block sl = { s[0].start,        s[0].start + wl - 1 };
        Block sr = { s[0].start + wl,   s[0].end };
        MovingBlock b0, b1;
        b0.srcStart = sl.start; b0.srcEnd = sl.end; b0.colorIndex = 0;
        offscreenDest(EDGE_LEFT,  sl.width(), stripLen, b0.dstStart, b0.dstEnd);
        b1.srcStart = sr.start; b1.srcEnd = sr.end; b1.colorIndex = 0;
        offscreenDest(EDGE_RIGHT, sr.width(), stripLen, b1.dstStart, b1.dstEnd);
        plan.blocks[plan.count++] = b0;
        plan.blocks[plan.count++] = b1;
        return plan;
    }

    for (int i = 0; i < ns; ++i) {
        EdgeMode e = edge;
        if (edge == EDGE_NEAREST) e = nearestEdge(s[i].center(), stripLen);
        else if (edge == EDGE_BOTH) e = (i == 0) ? EDGE_LEFT : EDGE_RIGHT;
        MovingBlock b;
        b.srcStart = s[i].start; b.srcEnd = s[i].end;
        offscreenDest(e, s[i].width(), stripLen, b.dstStart, b.dstEnd);
        b.colorIndex = i;
        plan.blocks[plan.count++] = b;
    }
    return plan;
}

} // namespace ceiling

#endif // CEILING_MOTION_H
