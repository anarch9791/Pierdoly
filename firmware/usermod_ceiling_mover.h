// usermod_ceiling_mover.h
// -----------------------------------------------------------------------------
// WLED usermod: "Ceiling Mover"
//
// Purpose
//   Make a ceiling light behave like a normal static WLED light, but when the
//   user switches between predefined layouts, physically MOVE the illuminated
//   block(s) across the strip (accelerate -> cruise -> decelerate -> stop
//   exactly on the destination). Also provides animated ON / OFF.
//
// Design summary (see docs/DESIGN.md for the full rationale)
//   * A custom effect ("Ceiling Move") is registered at runtime via
//     strip.addEffect(), so FX.cpp / FX.h are NOT edited -> minimal core churn.
//   * All planning + kinematics live in ceiling_motion.h (pure, unit-tested).
//   * COLOUR SAFETY: at the start of a move we sample the already-resolved
//     per-pixel RGBW (strip.getPixelColor) of the current layout and re-emit
//     those EXACT uint32 values, only shifted in position. We never rebuild a
//     colour, never touch auto-white, gamma, brightness scaling or colour
//     order. Everything downstream (auto-white, global brightness, colour
//     order, per-bus output) is the identical WLED path used by the static
//     renderer, so nothing gets brighter/dimmer or shifts hue while moving.
//   * When a move finishes we lay down normal WLED "Solid" segments matching
//     the destination blocks, with transition time 0, in the same service
//     cycle. The last animated frame is pixel-identical to that settled image,
//     so there is no black frame, no flash and no crossfade. The settled state
//     is ordinary WLED state: static, UI/HA controllable, no realtime/DDP.
//
// PORTING NOTES (verify against your WLED 0.16.x tree; marked  // VERIFY: )
//   A few helper APIs have drifted between WLED versions. Each call that could
//   differ is tagged so you can confirm the signature. Nothing here needs core
//   edits; only these names might need a one-line adjustment.
// -----------------------------------------------------------------------------
#pragma once

#include "wled.h"
#include "ceiling_motion.h"

using namespace ceiling;

// ------------------------------ configuration -------------------------------
#ifndef CEILING_STRIP_LEN
  #define CEILING_STRIP_LEN 864          // 6 m * 144 LED/m
#endif
#ifndef CEILING_NUM_LAYOUTS
  #define CEILING_NUM_LAYOUTS 5
#endif

// Unique usermod id. Guard-defined so const.h need not be edited; pick any
// value not already used by another usermod you have enabled.
#ifndef USERMOD_ID_CEILING_MOVER
  #define USERMOD_ID_CEILING_MOVER 0xC5
#endif

// RGB->RGBW auto-white modes, exposed to Home Assistant as a simple selector.
// VERIFY: these numeric values must match your WLED const.h RGBW_MODE_* defines.
// In current WLED: 0 MANUAL_ONLY, 1 AUTO_BRIGHTER, 2 AUTO_ACCURATE, 3 DUAL,
// 4 MAX(legacy). The HA-facing names Accurate/Brighter/Max map to 2/1/4.
#ifndef CEILING_AWM_ACCURATE
  #define CEILING_AWM_ACCURATE 2
#endif
#ifndef CEILING_AWM_BRIGHTER
  #define CEILING_AWM_BRIGHTER 1
#endif
#ifndef CEILING_AWM_MAX
  #define CEILING_AWM_MAX      4
#endif

class CeilingMover : public Usermod {
  public:
    // ---- lifecycle ---------------------------------------------------------
    void setup() override;
    void loop() override;
    void connected() override {}

    // ---- JSON state API (Home Assistant / /json/state) ---------------------
    void addToJsonState(JsonObject& root) override;
    void readFromJsonState(JsonObject& root) override;

    // ---- persistent config (/json/cfg, settings UI) ------------------------
    void addToConfig(JsonObject& root) override;
    bool readFromConfig(JsonObject& root) override;

    uint16_t getId() override { return USERMOD_ID_CEILING_MOVER; }

    // ---- called by the registered effect function --------------------------
    uint16_t renderFrame();          // draws one animation frame into SEGMENT
    bool     isAnimating() const { return _state == ANIMATING; }

    static CeilingMover* instance;   // effect fn reaches the usermod via this

  private:
    enum State { SETTLED, ANIMATING };

    // --- installation model -------------------------------------------------
    int          _stripLen = CEILING_STRIP_LEN;
    Layout       _layouts[CEILING_NUM_LAYOUTS];
    uint32_t     _layoutColor[CEILING_NUM_LAYOUTS]; // settle colour per layout
    MotionParams _motion   = MotionParams::defaults();

    // --- runtime state ------------------------------------------------------
    State          _state       = SETTLED;
    bool           _lightOn      = false;
    int            _curLayout    = 0;       // currently displayed layout (-1 none)
    TransitionPlan _plan;
    uint32_t       _blockColor[MAX_BLOCKS] = {0, 0};
    float          _duration    = 0.0f;     // seconds
    uint32_t       _startMs     = 0;
    int            _settleLayout = -1;      // layout to settle to (-1 == off)
    uint8_t        _customFxId  = 255;      // id assigned to our effect
    bool           _finalizePending = false;

    // --- helpers ------------------------------------------------------------
    void   loadDefaults();
    Layout currentVisibleLayout() const;
    void   captureSourceColors(const Layout& src);
    void   beginAnimation(const TransitionPlan& plan, int settleLayout);
    void   finalizeToSettled();
    void   finalizeToSettledDirect(int layoutIdx); // "no animation" jump
    void   settleLayoutSegments(int layoutIdx);   // -1 => everything off
    void   setAutoWhiteMode(uint8_t m);

    // action entry points (called from readFromJsonState)
    void   actionGoto(int layoutIdx, bool noAnim);
    void   actionOn(EdgeMode edge, int layoutIdx, bool noAnim);
    void   actionOff(EdgeMode edge, bool noAnim);
};

// ---------- effect data string + registered mode function -------------------
// VERIFY: effect metadata format "Name@params;colors;palette;flags". We take no
// speed/intensity sliders (motion is driven by the usermod), 1D, RGBW-capable.
static const char _data_FX_MODE_CEILINGMOVE[] PROGMEM = "Ceiling Move@;;;01";

// Free function required by strip.addEffect(). Delegates to the usermod.
uint16_t mode_ceilingMove() {
    if (CeilingMover::instance && CeilingMover::instance->isAnimating())
        return CeilingMover::instance->renderFrame();
    // Not animating: hold black so a stray mode assignment can't flash colour.
    SEGMENT.fill(BLACK);
    return FRAMETIME;
}

// ============================================================================
//                              IMPLEMENTATION
// ============================================================================
CeilingMover* CeilingMover::instance = nullptr;

void CeilingMover::setup() {
    instance = this;
    loadDefaults();
    // Register our custom effect. id 255 => auto-assign next free slot.
    // VERIFY: WS2812FX::addEffect(uint8_t id, mode_ptr, const char*) -> uint8_t.
    _customFxId = strip.addEffect(255, &mode_ceilingMove, _data_FX_MODE_CEILINGMOVE);
}

void CeilingMover::loadDefaults() {
    // The five predefined layouts from the specification.
    // count/blocks; colours default to warm white-ish, overwritten by cfg/UI.
    auto set1 = [&](int i, int s, int e) {
        _layouts[i].count = 1; _layouts[i].blocks[0] = {s, e};
    };
    auto set2 = [&](int i, int s0, int e0, int s1, int e1) {
        _layouts[i].count = 2;
        _layouts[i].blocks[0] = {s0, e0};
        _layouts[i].blocks[1] = {s1, e1};
    };
    set1(0, 0, 863);          // Layout 1
    set1(1, 100, 400);        // Layout 2
    set1(2, 500, 800);        // Layout 3
    set2(3, 50, 200, 700, 800);   // Layout 4 (two blocks)
    set1(4, 300, 563);        // Layout 5

    // Default settle colour: neutral warm (R,G,B,W). Tune / set from UI or HA.
    for (int i = 0; i < CEILING_NUM_LAYOUTS; ++i)
        _layoutColor[i] = RGBW32(255, 170, 60, 200);
}

// Determine the layout currently on screen (for the source of a move).
Layout CeilingMover::currentVisibleLayout() const {
    if (!_lightOn || _curLayout < 0) { Layout l; l.count = 0; return l; }
    return _layouts[_curLayout];
}

// Sample the ALREADY-RESOLVED per-pixel RGBW at each source block's centre and
// store it, so the moving block re-emits the exact same value (no reconversion).
void CeilingMover::captureSourceColors(const Layout& src) {
    for (int i = 0; i < MAX_BLOCKS; ++i) _blockColor[i] = 0;
    for (int i = 0; i < src.count && i < MAX_BLOCKS; ++i) {
        int c = src.blocks[i].center();
        if (c < 0) c = 0; if (c > _stripLen - 1) c = _stripLen - 1;
        // VERIFY: WS2812FX::getPixelColor(uint16_t) -> uint32_t (post-effect,
        // pre-bus). This is the settled colour we must preserve exactly.
        _blockColor[i] = strip.getPixelColor(c);
    }
}

void CeilingMover::beginAnimation(const TransitionPlan& plan, int settleLayout) {
    _plan          = plan;
    _settleLayout  = settleLayout;
    _duration      = planDuration(planMaxDistance(plan), _motion);
    _startMs       = millis();
    _finalizePending = false;

    if (_duration <= 0.0f) {           // "no animation" / zero-distance
        finalizeToSettled();
        return;
    }

    // Switch to a single full-strip segment running our effect, with NO
    // crossfade. The effect's first frame (s=0) reproduces the current image
    // exactly, so this swap is visually seamless.
    strip.setTransition(0);                         // VERIFY: setTransition(ms)
    strip.setMainSegmentId(0);                      // VERIFY: helper exists
    // Collapse to one segment covering the whole logical strip.
    strip.getSegment(0).setUp(0, _stripLen, 1, 0, 0);   // VERIFY: Segment::setUp
    if (strip.getSegmentsNum() > 1) {
        for (uint8_t s = 1; s < strip.getSegmentsNum(); ++s)
            strip.getSegment(s).setOption(SEG_OPTION_ON, false); // hide others
    }
    Segment& seg = strip.getSegment(0);
    seg.setOption(SEG_OPTION_ON, true);
    seg.setMode(_customFxId);                        // VERIFY: Segment::setMode
    _state = ANIMATING;
    strip.trigger();                                 // request immediate service
}

// Draw one frame of the moving block(s). Runs inside the effect (SEGMENT valid).
uint16_t CeilingMover::renderFrame() {
    float t = (millis() - _startMs) / 1000.0f;
    float s = easedProgress(t, _duration, _motion);

    SEGMENT.fill(BLACK);                             // dark background each frame
    for (int i = 0; i < _plan.count; ++i) {
        float a, b; blockSpanAt(_plan.blocks[i], s, a, b);
        int ia, ib;
        if (!spanToPixels(a, b, _stripLen, ia, ib)) continue;   // off-screen
        uint32_t col = _blockColor[_plan.blocks[i].colorIndex];
        for (int p = ia; p <= ib; ++p)
            SEGMENT.setPixelColor(p, col);           // exact captured RGBW
    }

    if (s >= 1.0f) _finalizePending = true;          // loop() will settle
    // Target ~50-55 FPS; frame delay in ms.
    int fps = _motion.fps > 0 ? _motion.fps : 50;
    return (uint16_t)(1000 / fps);
}

void CeilingMover::loop() {
    // Finalise on the service cycle AFTER the last (s>=1) frame was shown, so
    // the settled segments replace an already-identical image (no flash).
    if (_state == ANIMATING && _finalizePending)
        finalizeToSettled();
}

void CeilingMover::finalizeToSettled() {
    settleLayoutSegments(_settleLayout);
    _finalizePending = false;
    _state = SETTLED;
    if (_settleLayout >= 0) { _curLayout = _settleLayout; _lightOn = true; }
    else                    { _lightOn = false; }
}

// Lay down normal WLED Solid segments matching the destination layout. Pixels
// outside the segments are left off. transition=0 so nothing crossfades.
// settleLayoutIdx < 0 means "off": clear the strip and turn the segment off.
void CeilingMover::settleLayoutSegments(int layoutIdx) {
    strip.setTransition(0);

    if (layoutIdx < 0) {                              // OFF: clear everything
        strip.getSegment(0).setUp(0, _stripLen, 1, 0, 0);
        strip.getSegment(0).setMode(FX_MODE_STATIC);  // Solid
        strip.getSegment(0).setColor(0, BLACK);
        strip.getSegment(0).setOption(SEG_OPTION_ON, false);
        strip.fill(BLACK);                            // VERIFY: WS2812FX::fill
        return;
    }

    const Layout& L = _layouts[layoutIdx];
    uint32_t col    = _layoutColor[layoutIdx];

    // Clear the whole strip once so non-segment pixels stay dark, then map one
    // Solid segment per block. (For a 1-block layout this is a single segment.)
    strip.fill(BLACK);
    for (int i = 0; i < L.count && i < MAX_BLOCKS; ++i) {
        Segment& seg = strip.getSegment(i);
        // Segment stop index is exclusive in WLED (start, stop).
        seg.setUp(L.blocks[i].start, L.blocks[i].end + 1, 1, 0, 0);
        seg.setMode(FX_MODE_STATIC);
        seg.setColor(0, col);
        seg.setOption(SEG_OPTION_ON, true);
    }
    // Disable any leftover segments beyond those we use.
    for (uint8_t s = L.count; s < strip.getSegmentsNum(); ++s)
        strip.getSegment(s).setOption(SEG_OPTION_ON, false);

    strip.setMainSegmentId(0);
}

void CeilingMover::setAutoWhiteMode(uint8_t m) {
    // VERIFY: Bus::setGlobalAWMode(uint8_t) sets the global RGB->RGBW mode used
    // by every bus. This is the same conversion the static renderer uses, so
    // changing it here affects both static and moving states identically.
    Bus::setGlobalAWMode(m);
    strip.trigger();
}

// ------------------------------ actions -------------------------------------
void CeilingMover::actionGoto(int layoutIdx, bool noAnim) {
    if (layoutIdx < 0 || layoutIdx >= CEILING_NUM_LAYOUTS) return;
    if (!_lightOn) { actionOn(EDGE_NEAREST, layoutIdx, noAnim); return; }

    Layout src = currentVisibleLayout();
    captureSourceColors(src);
    // Destination blocks inherit their painting colour: for split (1->2) both
    // children reuse source colour 0; that is already how the plan indexes.
    TransitionPlan plan = planTransition(src, _layouts[layoutIdx]);
    if (noAnim) { finalizeToSettledDirect(layoutIdx); return; }
    beginAnimation(plan, layoutIdx);
}

void CeilingMover::actionOn(EdgeMode edge, int layoutIdx, bool noAnim) {
    if (layoutIdx < 0 || layoutIdx >= CEILING_NUM_LAYOUTS) layoutIdx = _curLayout;
    if (layoutIdx < 0) layoutIdx = 0;
    // Incoming blocks are painted with the target layout's settle colour.
    for (int i = 0; i < MAX_BLOCKS; ++i) _blockColor[i] = _layoutColor[layoutIdx];
    if (edge == EDGE_NONE || noAnim) { finalizeToSettledDirect(layoutIdx); return; }
    TransitionPlan plan = planOn(_layouts[layoutIdx], edge, _stripLen);
    beginAnimation(plan, layoutIdx);
}

void CeilingMover::actionOff(EdgeMode edge, bool noAnim) {
    if (!_lightOn) return;
    Layout src = currentVisibleLayout();
    captureSourceColors(src);
    if (edge == EDGE_NONE || noAnim) { finalizeToSettledDirect(-1); return; }
    TransitionPlan plan = planOff(src, edge, _stripLen);
    beginAnimation(plan, -1);   // settle to off
}

// Helper for the "no animation" path: jump straight to the settled state.
void CeilingMover::finalizeToSettledDirect(int layoutIdx) {
    _settleLayout = layoutIdx;
    finalizeToSettled();
}

// ------------------------------ JSON API ------------------------------------
// Accepts, under root["CeilingMover"]:
//   {"layout":N}                 goto layout N (0-based here; 1-based in HA docs)
//   {"noanim":true}              with an action -> skip the animation
//   {"on":{"edge":E,"layout":N}} animated ON  (E: 0 L,1 R,2 both,3 nearest,4 none)
//   {"off":{"edge":E}}           animated OFF
//   {"awm":M}                    RGBW auto-white mode (Accurate/Brighter/Max)
//   {"speed":px, "accel":f, "cruise":f, "decel":f, "fps":n}  motion overrides
void CeilingMover::readFromJsonState(JsonObject& root) {
    JsonObject o = root["CeilingMover"];
    if (o.isNull()) return;

    // motion overrides (optional, applied before the action)
    if (o.containsKey("speed"))  _motion.maxSpeed   = o["speed"].as<float>();
    if (o.containsKey("accel"))  _motion.accelFrac  = o["accel"].as<float>();
    if (o.containsKey("cruise")) _motion.cruiseFrac = o["cruise"].as<float>();
    if (o.containsKey("decel"))  _motion.decelFrac  = o["decel"].as<float>();
    if (o.containsKey("fps"))    _motion.fps        = o["fps"].as<int>();

    if (o.containsKey("awm")) setAutoWhiteMode((uint8_t)o["awm"].as<int>());

    bool noAnim = o["noanim"] | false;

    if (o.containsKey("off")) {
        JsonObject off = o["off"];
        actionOff((EdgeMode)(off["edge"] | (int)EDGE_NEAREST), noAnim);
        return;
    }
    if (o.containsKey("on")) {
        JsonObject on = o["on"];
        int lidx = on["layout"] | _curLayout;
        actionOn((EdgeMode)(on["edge"] | (int)EDGE_NEAREST), lidx, noAnim);
        return;
    }
    if (o.containsKey("layout")) {
        actionGoto(o["layout"].as<int>(), noAnim);
        return;
    }
}

void CeilingMover::addToJsonState(JsonObject& root) {
    JsonObject o = root.createNestedObject("CeilingMover");
    o["on"]       = _lightOn;
    o["layout"]   = _curLayout;
    o["state"]    = (_state == ANIMATING) ? "moving" : "settled";
    o["awm"]      = Bus::getGlobalAWMode();          // VERIFY: getter exists
    o["speed"]    = _motion.maxSpeed;
    o["fps"]      = _motion.fps;
}

// ------------------------------- config -------------------------------------
void CeilingMover::addToConfig(JsonObject& root) {
    JsonObject top = root.createNestedObject("CeilingMover");
    top["maxSpeed"]   = _motion.maxSpeed;
    top["accelFrac"]  = _motion.accelFrac;
    top["cruiseFrac"] = _motion.cruiseFrac;
    top["decelFrac"]  = _motion.decelFrac;
    top["fps"]        = _motion.fps;

    JsonArray arr = top.createNestedArray("layouts");
    for (int i = 0; i < CEILING_NUM_LAYOUTS; ++i) {
        JsonObject lo = arr.createNestedObject();
        lo["count"] = _layouts[i].count;
        lo["s0"] = _layouts[i].blocks[0].start; lo["e0"] = _layouts[i].blocks[0].end;
        lo["s1"] = _layouts[i].blocks[1].start; lo["e1"] = _layouts[i].blocks[1].end;
        lo["col"] = _layoutColor[i];
    }
}

bool CeilingMover::readFromConfig(JsonObject& root) {
    JsonObject top = root["CeilingMover"];
    if (top.isNull()) return false;
    _motion.maxSpeed   = top["maxSpeed"]   | _motion.maxSpeed;
    _motion.accelFrac  = top["accelFrac"]  | _motion.accelFrac;
    _motion.cruiseFrac = top["cruiseFrac"] | _motion.cruiseFrac;
    _motion.decelFrac  = top["decelFrac"]  | _motion.decelFrac;
    _motion.fps        = top["fps"]        | _motion.fps;

    JsonArray arr = top["layouts"];
    if (!arr.isNull()) {
        int i = 0;
        for (JsonObject lo : arr) {
            if (i >= CEILING_NUM_LAYOUTS) break;
            _layouts[i].count = lo["count"] | _layouts[i].count;
            _layouts[i].blocks[0].start = lo["s0"] | _layouts[i].blocks[0].start;
            _layouts[i].blocks[0].end   = lo["e0"] | _layouts[i].blocks[0].end;
            _layouts[i].blocks[1].start = lo["s1"] | _layouts[i].blocks[1].start;
            _layouts[i].blocks[1].end   = lo["e1"] | _layouts[i].blocks[1].end;
            _layoutColor[i]             = lo["col"] | _layoutColor[i];
            ++i;
        }
    }
    return true;
}
