#pragma once

namespace ao {

// Show the system software keyboard and return the user's input.
//
// On Switch: calls libnx SwkbdConfig + swkbdShow().
//            Works correctly on both real hardware and Ryujinx.
// On desktop (non-Switch): falls back to stdin readline for dev/testing.
//
// hint:     placeholder text shown in the keyboard
// initial:  pre-filled text
// out:      destination buffer
// out_cap:  max bytes including null terminator
// password: if true, input is masked
//
// Returns true if the user confirmed input, false if they cancelled.

bool show_keyboard(const char* hint,
                   const char* initial,
                   char*       out,
                   int         out_cap,
                   bool        password = false);

} // namespace ao
