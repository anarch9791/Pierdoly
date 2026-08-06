// test_motion.cpp
// -----------------------------------------------------------------------------
// Host-side test + visualiser for the Ceiling Mover motion core.
// Includes the SAME ceiling_motion.h that the firmware uses, so this proves the
// real planning/kinematics code, not a reimplementation.
//
// Build:   g++ -std=c++14 -O2 -I../firmware test_motion.cpp -o test_motion
//          (or use the Makefile in this directory)
// Run:     ./test_motion
//
// All menus use NUMBERED choices only.
// -----------------------------------------------------------------------------
#include "ceiling_motion.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

using namespace ceiling;

static const int STRIP = 864;   // 6 m * 144 LED/m
static const int COLS  = 108;   // ASCII visualisation width

// ------- the five predefined layouts from the specification (+ OFF) ----------
struct NamedLayout { const char* name; Layout layout; };

static Layout L1() { Layout l; l.count = 1; l.blocks[0] = {0, 863};   return l; }
static Layout L2() { Layout l; l.count = 1; l.blocks[0] = {100, 400}; return l; }
static Layout L3() { Layout l; l.count = 1; l.blocks[0] = {500, 800}; return l; }
static Layout L4() { Layout l; l.count = 2; l.blocks[0] = {50, 200};
                                            l.blocks[1] = {700, 800}; return l; }
static Layout L5() { Layout l; l.count = 1; l.blocks[0] = {300, 563}; return l; }

static std::vector<NamedLayout> layouts() {
    return {
        {"Layout 1: 0-863 (full)",         L1()},
        {"Layout 2: 100-400",              L2()},
        {"Layout 3: 500-800",              L3()},
        {"Layout 4: 50-200 & 700-800",     L4()},
        {"Layout 5: 300-563",              L5()},
    };
}

// ---------------------------- helpers ---------------------------------------

// Render a plan at progress s into an on/off pixel buffer (true == lit).
static void renderPlan(const TransitionPlan& plan, float s, std::vector<char>& px) {
    px.assign(STRIP, 0);
    for (int i = 0; i < plan.count; ++i) {
        float a, b; blockSpanAt(plan.blocks[i], s, a, b);
        int ia, ib;
        if (spanToPixels(a, b, STRIP, ia, ib))
            for (int p = ia; p <= ib; ++p) px[p] = 1;
    }
}

static void renderLayout(const Layout& L, std::vector<char>& px) {
    px.assign(STRIP, 0);
    for (int i = 0; i < L.count; ++i)
        for (int p = L.blocks[i].start; p <= L.blocks[i].end; ++p)
            if (p >= 0 && p < STRIP) px[p] = 1;
}

static void printBar(const std::vector<char>& px, const char* prefix) {
    std::string line(COLS, '.');
    for (int c = 0; c < COLS; ++c) {
        int p0 = (int)((long)c * STRIP / COLS);
        int p1 = (int)((long)(c + 1) * STRIP / COLS);
        if (p1 <= p0) p1 = p0 + 1;
        int lit = 0, tot = 0;
        for (int p = p0; p < p1 && p < STRIP; ++p) { tot++; if (px[p]) lit++; }
        if (tot && lit * 2 >= tot) line[c] = '#';
        else if (lit)              line[c] = '-';
    }
    printf("%s|%s|\n", prefix, line.c_str());
}

static int totalLit(const std::vector<char>& px) {
    int n = 0; for (char c : px) if (c) n++; return n;
}

static int readChoice(int lo, int hi) {
    for (;;) {
        printf("  > "); fflush(stdout);
        char buf[64];
        if (!fgets(buf, sizeof buf, stdin)) return lo;   // EOF: safe default
        char* end = nullptr;
        long v = strtol(buf, &end, 10);
        if (end != buf && v >= lo && v <= hi) return (int)v;
        printf("  Enter a number %d-%d.\n", lo, hi);
    }
}

// ------------------------------ test suite ----------------------------------
static int g_pass = 0, g_fail = 0;

static void check(bool ok, const std::string& what) {
    if (ok) { g_pass++; }
    else    { g_fail++; printf("    FAIL: %s\n", what.c_str()); }
}

// Verify a completed plan lands exactly on the expected pixel set.
static void verifyLanding(const TransitionPlan& plan, const Layout& expected,
                          const std::string& label) {
    std::vector<char> got, want;
    renderPlan(plan, 1.0f, got);
    renderLayout(expected, want);
    bool exact = (got == want);
    check(exact, label + " lands exactly on destination pixels");
    if (!exact) {
        int diff = 0;
        for (int i = 0; i < STRIP; ++i) if (got[i] != want[i]) diff++;
        printf("    (%d pixel(s) differ)\n", diff);
    }
}

// Verify rest-to-rest, monotonic, exact endpoints of the kinematics.
static void verifyKinematics(float T, const MotionParams& mp,
                             const std::string& label) {
    check(easedProgress(0.0f, T, mp) == 0.0f, label + " starts at s=0");
    check(easedProgress(T,    T, mp) == 1.0f, label + " ends at s=1");

    float dt = T / 400.0f;
    float prev = 0.0f; bool mono = true;
    for (int i = 0; i <= 400; ++i) {
        float s = easedProgress(i * dt, T, mp);
        if (s < prev - 1e-6f) mono = false;
        prev = s;
    }
    check(mono, label + " progress is monotonic");

    // velocity (numeric) near the ends must be far below mid-move velocity.
    float vStart = (easedProgress(dt, T, mp) - easedProgress(0, T, mp)) / dt;
    float vEnd   = (easedProgress(T, T, mp) - easedProgress(T - dt, T, mp)) / dt;
    float vMid   = (easedProgress(T/2 + dt, T, mp) - easedProgress(T/2, T, mp)) / dt;
    check(vStart < vMid * 0.15f, label + " accelerates from near-rest");
    check(vEnd   < vMid * 0.15f, label + " decelerates to near-rest");
}

static void runSuite() {
    printf("\n=== Automated transition suite ===\n\n");
    g_pass = g_fail = 0;
    auto L = layouts();
    MotionParams mp = MotionParams::defaults();

    // Every ordered pair of the 5 layouts (covers 1<->1, 1<->2, 2<->1, 2<->2).
    for (size_t i = 0; i < L.size(); ++i) {
        for (size_t j = 0; j < L.size(); ++j) {
            if (i == j) continue;
            std::string label = std::string(L[i].name) + " -> " + L[j].name;
            TransitionPlan plan = planTransition(L[i].layout, L[j].layout);

            // Moving-block count: 1->1 uses 1; anything involving 2 blocks
            // uses 2 (split for 1->2, converge for 2->1, pair for 2->2).
            int expectBlocks = (L[i].layout.count > L[j].layout.count)
                                 ? L[i].layout.count : L[j].layout.count;
            check(plan.count == expectBlocks, label + " has correct moving-block count");

            verifyLanding(plan, L[j].layout, label);

            // total lit width at end equals destination width
            std::vector<char> endpx; renderPlan(plan, 1.0f, endpx);
            int wantW = 0;
            for (int b = 0; b < L[j].layout.count; ++b)
                wantW += L[j].layout.blocks[b].width();
            check(totalLit(endpx) == wantW, label + " conserves destination width");

            // start frame matches the SOURCE exactly (no jump at t=0)
            std::vector<char> startpx, srcpx;
            renderPlan(plan, 0.0f, startpx);
            renderLayout(L[i].layout, srcpx);
            check(startpx == srcpx, label + " first frame matches source");

            float T = planDuration(planMaxDistance(plan), mp);
            verifyKinematics(T, mp, label);
        }
    }

    // ON animations into every layout, each edge mode
    const EdgeMode edges[] = {EDGE_LEFT, EDGE_RIGHT, EDGE_BOTH, EDGE_NEAREST};
    const char* enames[]   = {"LEFT", "RIGHT", "BOTH", "NEAREST"};
    for (size_t j = 0; j < L.size(); ++j) {
        for (int e = 0; e < 4; ++e) {
            std::string label = "ON " + std::string(enames[e]) + " -> " + L[j].name;
            TransitionPlan plan = planOn(L[j].layout, edges[e], STRIP);
            verifyLanding(plan, L[j].layout, label);
            // first frame must be fully off-screen (dark)
            std::vector<char> startpx; renderPlan(plan, 0.0f, startpx);
            check(totalLit(startpx) == 0, label + " starts dark (off-screen)");
        }
    }

    // OFF animations from every layout, each edge mode: must end fully dark
    for (size_t i = 0; i < L.size(); ++i) {
        for (int e = 0; e < 4; ++e) {
            std::string label = "OFF " + std::string(enames[e]) + " <- " + L[i].name;
            TransitionPlan plan = planOff(L[i].layout, edges[e], STRIP);
            std::vector<char> endpx; renderPlan(plan, 1.0f, endpx);
            check(totalLit(endpx) == 0, label + " ends fully dark");
            std::vector<char> startpx, srcpx;
            renderPlan(plan, 0.0f, startpx);
            renderLayout(L[i].layout, srcpx);
            check(startpx == srcpx, label + " first frame matches source");
        }
    }

    printf("\n  Passed: %d   Failed: %d\n", g_pass, g_fail);
    printf("  %s\n\n", g_fail == 0 ? "ALL TESTS PASSED" : "*** FAILURES PRESENT ***");
}

// --------------------------- visualisers ------------------------------------
static void animate(const TransitionPlan& plan, int frames) {
    MotionParams mp = MotionParams::defaults();
    float T = planDuration(planMaxDistance(plan), mp);
    printf("  duration = %.2fs, maxSpeed = %.0f px/s, fps = %d\n\n",
           T, mp.maxSpeed, mp.fps);
    std::vector<char> px;
    for (int f = 0; f <= frames; ++f) {
        float t = (frames > 0) ? (T * f / frames) : T;
        float s = easedProgress(t, T, mp);
        renderPlan(plan, s, px);
        char pref[16]; snprintf(pref, sizeof pref, "s=%.2f ", s);
        printBar(px, pref);
    }
    printf("\n");
}

static int pickLayout(const char* prompt) {
    auto L = layouts();
    printf("%s\n", prompt);
    for (size_t i = 0; i < L.size(); ++i) printf("  %zu) %s\n", i + 1, L[i].name);
    return readChoice(1, (int)L.size()) - 1;
}

static EdgeMode pickEdge() {
    printf("Choose edge mode:\n");
    printf("  1) Left edge\n  2) Right edge\n  3) Both edges\n  4) Nearest edge\n");
    switch (readChoice(1, 4)) {
        case 1: return EDGE_LEFT;
        case 2: return EDGE_RIGHT;
        case 3: return EDGE_BOTH;
        default: return EDGE_NEAREST;
    }
}

static void showProfile() {
    MotionParams mp = MotionParams::defaults();
    float T = 2.0f;
    printf("\n  Kinematics profile (accel %.2f / cruise %.2f / decel %.2f):\n\n",
           mp.accelFrac, mp.cruiseFrac, mp.decelFrac);
    printf("   t/T     s(progress)   v(normalised)\n");
    float dt = T / 20.0f;
    for (int i = 0; i <= 20; ++i) {
        float t = i * dt;
        float s = easedProgress(t, T, mp);
        float v = (easedProgress(t + 1e-3f, T, mp) - s) / 1e-3f * T;
        int bar = (int)(v * 30.0f / 1.6f);
        std::string b(bar < 0 ? 0 : bar, '#');
        printf("  %.2f     %.4f        %s\n", t / T, s, b.c_str());
    }
    printf("\n");
}

int main() {
    printf("Ceiling Mover - motion core test utility (strip = %d pixels)\n", STRIP);
    for (;;) {
        printf("\n==== MENU ====\n");
        printf("  1) Run automated transition test suite\n");
        printf("  2) Visualise a layout -> layout transition\n");
        printf("  3) Visualise an ON animation\n");
        printf("  4) Visualise an OFF animation\n");
        printf("  5) Show kinematics profile\n");
        printf("  0) Exit\n");
        int c = readChoice(0, 5);
        if (c == 0) break;
        switch (c) {
            case 1: runSuite(); break;
            case 2: {
                auto L = layouts();
                int a = pickLayout("Source layout:");
                int b = pickLayout("Destination layout:");
                printf("\n  %s  ->  %s\n\n", L[a].name, L[b].name);
                animate(planTransition(L[a].layout, L[b].layout), 16);
                break;
            }
            case 3: {
                auto L = layouts();
                int b = pickLayout("Destination layout:");
                EdgeMode e = pickEdge();
                printf("\n  ON -> %s\n\n", L[b].name);
                animate(planOn(L[b].layout, e, STRIP), 16);
                break;
            }
            case 4: {
                auto L = layouts();
                int a = pickLayout("Source layout:");
                EdgeMode e = pickEdge();
                printf("\n  OFF <- %s\n\n", L[a].name);
                animate(planOff(L[a].layout, e, STRIP), 16);
                break;
            }
            case 5: showProfile(); break;
        }
    }
    return 0;
}
