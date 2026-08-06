// usermods_list_ceiling.cpp
// -----------------------------------------------------------------------------
// Reference for the two edits WLED needs to activate the Ceiling Mover usermod.
// This file is NOT compiled as-is; it shows exactly what to add to WLED's own
// wled00/usermods_list.cpp. (See docs/INSTALL.md, stage 8.)
//
// 1) Near the other usermod includes at the top of usermods_list.cpp:
//
//      #ifdef USERMOD_CEILING_MOVER
//      #include "../usermods/ceiling_mover/usermod_ceiling_mover.h"
//      #endif
//
// 2) Inside registerUsermods() (the body of that function):
//
//      #ifdef USERMOD_CEILING_MOVER
//      usermods.add(new CeilingMover());
//      #endif
//
// Then define -D USERMOD_CEILING_MOVER in your build_flags (see
// platformio_override.ini). That is the ONLY change to WLED core source: two
// guarded lines in one existing file. The effect itself is registered at
// runtime via strip.addEffect(), so FX.cpp / FX.h are untouched.
// -----------------------------------------------------------------------------
