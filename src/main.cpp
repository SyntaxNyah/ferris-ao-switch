#include "app.hpp"
#include "ui/screen.hpp"
#include "ui/screens/connect_screen.hpp"
#include <cstdio>

int main(int /*argc*/, char* /*argv*/[]) {
    ao::App app;

    if (!app.init()) {
        std::fprintf(stderr, "App::init() failed — aborting\n");
        return 1;
    }

    // Start at the connect screen
    app.push_screen(new ao::ConnectScreen(app));
    app.run();

    return 0;
}
