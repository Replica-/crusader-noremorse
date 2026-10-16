#pragma once
// Numbering of the intrinsics. The port binds them under the numbers of the
// French usecode (USECODE/UNKCOFF.DAT of the French CRUSADER.EXE, 309
// entries). The US usecode (v1.23, 311 entries) calls the same routines under
// other numbers; this table gives, for each US number, the French number of
// the same routine. It was built by matching the two UNKCOFF.DAT tables both by
// ScummVM's names and by the code of the routines in the two executables.

#include <cstdint>
#include <span>

namespace crusader {

// Index: number called by the US usecode; value: French number.
std::span<const std::uint16_t> us_intrinsic_numbering();

}  // namespace crusader
