#include "virtual_keyboard.hpp"
#include <cstring>
#include <cstdio>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace ao {

bool show_keyboard(const char* hint,
                   const char* initial,
                   char*       out,
                   int         out_cap,
                   bool        password) {
#ifdef __SWITCH__
    SwkbdConfig kbd;
    Result rc = swkbdCreate(&kbd, 0);
    if (R_FAILED(rc)) return false;

    swkbdConfigSetGuideText(&kbd, hint ? hint : "");
    if (initial && initial[0])
        swkbdConfigSetInitialText(&kbd, initial);
    if (password)
        swkbdConfigSetPasswordFlag(&kbd, true);
    swkbdConfigSetStringLenMax(&kbd, out_cap - 1);

    rc = swkbdShow(&kbd, out, (size_t)out_cap);
    swkbdClose(&kbd);
    return R_SUCCEEDED(rc);

#else
    // Desktop fallback: print the hint and read from stdin
    std::printf("%s", hint ? hint : "Input: ");
    if (initial && initial[0])
        std::printf(" [%s]: ", initial);
    std::fflush(stdout);

    if (!std::fgets(out, out_cap, stdin)) {
        out[0] = '\0';
        return false;
    }
    // Strip newline
    int len = (int)std::strlen(out);
    while (len > 0 && (out[len-1] == '\n' || out[len-1] == '\r'))
        out[--len] = '\0';

    // If user just pressed Enter with no input, use initial value
    if (len == 0 && initial && initial[0])
        std::strncpy(out, initial, out_cap - 1);

    return true;
#endif
}

} // namespace ao
