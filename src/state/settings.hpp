#pragma once

namespace ao {

// User settings persisted to sdmc:/switch/ferris-ao/config.ini (a plain
// key = value INI) so they survive across servers and across launches. Loaded
// once in App::init(); save() is called whenever a value changes.
//
// On non-Switch builds the file lives at ./config.ini.
struct Settings {
    char showname[64]    = "";     // custom IC/OOC showname ("" = use username)
    char theme[64]       = "default";
    char master_url[256] = "https://servers.aceattorneyonline.com/servers";
    char last_host[256]  = "";     // remembered Direct-connect host
    char last_port[8]    = "27017";
    int  sfx_volume      = 96;     // 0-128 (MIX_MAX_VOLUME)
    int  music_volume    = 80;     // 0-128

    void load();   // parse the config file (missing keys keep their defaults)
    void save();   // write the config file (best-effort; creates the dir)

    // Absolute path of the config file for the current platform.
    static const char* path();
};

} // namespace ao
