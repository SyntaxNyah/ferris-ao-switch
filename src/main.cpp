#include "app.hpp"
#include "ui/screen.hpp"
#include "ui/screens/connect_screen.hpp"
#include <cstdio>
#include <csignal>

int main(int /*argc*/, char* /*argv*/[]) {
    // Never let a write to a peer-closed socket take down the app. AO servers
    // commonly close the connection on us (kick, WS Close, idle); without this
    // a SIGPIPE during the resulting send/close could terminate the process.
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);
#endif

    // Redirect stderr to SD card so debug prints are visible on Ryujinx/hardware.
    // Check sdmc:/ferris-ao-debug.log after running.
    if (std::freopen("sdmc:/ferris-ao-debug.log", "w", stderr))
        std::setvbuf(stderr, nullptr, _IONBF, 0); // unbuffered — flush on every write
    std::fprintf(stderr, "ferris-ao starting\n");

    // App contains large fixed-size queue buffers (~1.15 MB total) that would
    // overflow the Switch main-thread stack (~1 MB default) if stack-allocated.
    ao::App* app = new ao::App;

    if (!app->init()) {
        std::fprintf(stderr, "App::init() failed — aborting\n");
        delete app;
        return 1;
    }

    // Start at the connect screen
    app->push_screen(new ao::ConnectScreen(*app));
    app->run();

    delete app;
    return 0;
}
