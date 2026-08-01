// components/twc_director/twc_lib_shim.cpp
//
// Historical shim: the pure-C TWC protocol sources (twc_*.c) are now compiled
// as normal C translation units in this component directory.
// ESPHome 2026.7+ does not copy nested non-package subdirs of external
// components, so the library was flattened out of twc/ into this folder.
//
// This file remains so older references stay valid; it intentionally has no
// symbols.

namespace esphome {
namespace twc_director {
// intentionally empty
}  // namespace twc_director
}  // namespace esphome
