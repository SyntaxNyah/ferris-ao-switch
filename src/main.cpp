#include "app.hpp"
#include "ui/screen.hpp"
#include "ui/screens/connect_screen.hpp"
#include <cstdio>

int main(int /*argc*/, char* /*argv*/[]) {
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
