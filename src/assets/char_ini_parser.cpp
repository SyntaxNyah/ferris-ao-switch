#include "char_ini_parser.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace ao {

// Minimal Windows INI parser.
// Handles [Section], key = value, and ignores comments (;/#).

static void trim(char* s) {
    // Leading
    int start = 0;
    while (s[start] == ' ' || s[start] == '\t') ++start;
    if (start > 0) std::memmove(s, s + start, std::strlen(s + start) + 1);
    // Trailing
    int len = (int)std::strlen(s);
    while (len > 0 && (s[len-1]==' ' || s[len-1]=='\t' ||
                        s[len-1]=='\r'|| s[len-1]=='\n'))
        s[--len] = '\0';
}

bool parse_char_ini(const char* path, CharDef& out) {
    FILE* f = std::fopen(path, "r");
    if (!f) return false;

    std::memset(&out, 0, sizeof(out));
    int emotion_number_seen = 0; // last "number = N" seen in [Emotions]

    char section[64] = {};
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        trim(line);
        if (!line[0] || line[0]==';' || line[0]=='#') continue;

        if (line[0] == '[') {
            // Section header
            char* end = std::strchr(line + 1, ']');
            if (end) {
                int len = (int)(end - line - 1);
                if (len > 63) len = 63;
                std::strncpy(section, line + 1, len);
                section[len] = '\0';
            }
            continue;
        }

        // key = value
        char* eq = std::strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = line;
        char* val = eq + 1;
        trim(key);
        trim(val);

        if (std::strcmp(section, "Options") == 0) {
            if (std::strcmp(key, "name")     == 0) std::strncpy(out.name,     val, 63);
            if (std::strcmp(key, "showname") == 0) std::strncpy(out.showname, val, 63);
        } else if (std::strcmp(section, "Emotions") == 0) {
            if (std::strcmp(key, "number") == 0) {
                emotion_number_seen = std::atoi(val);
            } else {
                // Emotion entry: "1 = Thinking#-#normal#0"
                // or              "1 = Thinking#preanim#normal#0"
                int idx = std::atoi(key) - 1; // 1-based → 0-based
                if (idx < 0 || idx >= MAX_EMOTIONS) continue;
                if (idx >= out.emotion_count) out.emotion_count = idx + 1;

                EmotionEntry& em = out.emotions[idx];
                // Split val on '#'
                char parts[4][64] = {};
                int pi = 0;
                char tmp[256];
                std::strncpy(tmp, val, sizeof(tmp) - 1);
                char* tok = tmp;
                for (int i = 0; i < 4 && *tok; ) {
                    char* sep = std::strchr(tok, '#');
                    int plen;
                    if (sep) { plen = (int)(sep - tok); *sep = '\0'; }
                    else      { plen = (int)std::strlen(tok); }
                    if (plen > 63) plen = 63;
                    std::strncpy(parts[i], tok, plen);
                    ++i;
                    tok = sep ? sep + 1 : tok + plen;
                    if (!sep) break;
                }
                // parts[0]=name, parts[1]=pre_anim ("-" means none),
                // parts[2]=anim_base, parts[3]=desk_mod
                std::strncpy(em.name,      parts[0], 31);
                std::strncpy(em.idle_anim, parts[2], 63);
                std::strncpy(em.talk_anim, parts[2], 63);
                em.desk_mod = std::atoi(parts[3]);
                em.has_pre  = (parts[1][0] != '\0' && std::strcmp(parts[1], "-") != 0);
                if (em.has_pre) std::strncpy(em.pre_anim, parts[1], 63);
            }
        }
    }
    std::fclose(f);
    (void)emotion_number_seen;
    return true;
}

} // namespace ao
