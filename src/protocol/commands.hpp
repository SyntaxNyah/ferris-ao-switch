#pragma once
#include "packet.hpp"
#include <cstdio>
#include <cstring>

// Outgoing packet builders.
// All write into a caller-supplied stack buffer and return the byte count.
// Caller pushes the result to OutQueue.

namespace ao {
namespace cmd {

// HI#<hdid>#%
inline int hi(char* buf, int cap, const char* hdid) {
    char esc[256]; Packet::escape(hdid, esc, sizeof(esc));
    return std::snprintf(buf, cap, "HI#%s#%%", esc);
}

// ID#<software>#<version>#%
inline int id(char* buf, int cap,
              const char* software = "ferris-ao-switch-v1",
              const char* version  = "2.999.999") {
    return std::snprintf(buf, cap, "ID#%s#%s#%%", software, version);
}

// askchaa#%
inline int askchaa(char* buf, int cap) {
    return std::snprintf(buf, cap, "askchaa#%%");
}

// RC#%
inline int rc(char* buf, int cap) {
    return std::snprintf(buf, cap, "RC#%%");
}

// RM#%
inline int rm(char* buf, int cap) {
    return std::snprintf(buf, cap, "RM#%%");
}

// RD#%
inline int rd(char* buf, int cap) {
    return std::snprintf(buf, cap, "RD#%%");
}

// CH#%  (keepalive ping)
inline int ch(char* buf, int cap) {
    return std::snprintf(buf, cap, "CH#%%");
}

// CC#<uid>#<char_id>#<hdid>#%
inline int cc(char* buf, int cap, int uid, int char_id, const char* hdid) {
    char esc[256]; Packet::escape(hdid, esc, sizeof(esc));
    return std::snprintf(buf, cap, "CC#%d#%d#%s#%%", uid, char_id, esc);
}

// CT#<username>#<message>#%
inline int ct(char* buf, int cap, const char* username, const char* msg) {
    char eu[128], em[1024];
    Packet::escape(username, eu, sizeof(eu));
    Packet::escape(msg,      em, sizeof(em));
    return std::snprintf(buf, cap, "CT#%s#%s#%%", eu, em);
}

// MC#<song>#<char_id>#<showname>#1#0#0#%
inline int mc(char* buf, int cap,
              const char* song, int char_id, const char* showname) {
    char es[256], en[64];
    Packet::escape(song,     es, sizeof(es));
    Packet::escape(showname, en, sizeof(en));
    return std::snprintf(buf, cap, "MC#%s#%d#%s#1#0#0#%%", es, char_id, en);
}

// HP#<bar>#<value>#%
inline int hp(char* buf, int cap, int bar, int value) {
    return std::snprintf(buf, cap, "HP#%d#%d#%%", bar, value);
}

// PE#<name>#<desc>#<img>#%
inline int pe(char* buf, int cap,
              const char* name, const char* desc, const char* img) {
    char en[256], ed[512], ei[256];
    Packet::escape(name, en, sizeof(en));
    Packet::escape(desc, ed, sizeof(ed));
    Packet::escape(img,  ei, sizeof(ei));
    return std::snprintf(buf, cap, "PE#%s#%s#%s#%%", en, ed, ei);
}

// DE#<index>#%
inline int de(char* buf, int cap, int idx) {
    return std::snprintf(buf, cap, "DE#%d#%%", idx);
}

// EE#<index>#<name>#<desc>#<img>#%
inline int ee(char* buf, int cap, int idx,
              const char* name, const char* desc, const char* img) {
    char en[256], ed[512], ei[256];
    Packet::escape(name, en, sizeof(en));
    Packet::escape(desc, ed, sizeof(ed));
    Packet::escape(img,  ei, sizeof(ei));
    return std::snprintf(buf, cap, "EE#%d#%s#%s#%s#%%", idx, en, ed, ei);
}

// ZZ#<reason>#%
inline int zz(char* buf, int cap, const char* reason = "") {
    char er[256]; Packet::escape(reason, er, sizeof(er));
    return std::snprintf(buf, cap, "ZZ#%s#%%", er);
}

// RT#<type>#%
inline int rt(char* buf, int cap, const char* type) {
    return std::snprintf(buf, cap, "RT#%s#%%", type);
}

// SETCASE#<title>#<cm>#<def>#<pro>#<wit>#<judge>#<jur>#%
inline int setcase(char* buf, int cap,
                   const char* title = "",
                   bool cm=false, bool def_=false, bool pro=false,
                   bool wit=false, bool judge=false, bool jur=false) {
    char et[256]; Packet::escape(title, et, sizeof(et));
    return std::snprintf(buf, cap,
        "SETCASE#%s#%d#%d#%d#%d#%d#%d#%%",
        et, cm, def_, pro, wit, judge, jur);
}

// Build the MS (IC message) packet from an ICAnimState.
// Returns bytes written.  buf should be ~2048 bytes.
struct MSParams {
    int   desk_mod     = 0;
    char  pre_anim[64] = {};
    char  char_name[64]= {};
    char  emote[64]    = {};
    char  message[512] = {};
    char  pos[16]      = "def";
    char  sfx[64]      = "1";
    int   emote_mod    = 0;
    int   char_id      = 0;
    int   clip         = 0;
    int   objection    = 0;
    int   evidence_id  = 0;
    int   flip         = 0;
    int   realization  = 0;
    int   text_color   = 0;
    char  showname[64] = {};
    int   other_charid = -1;
    char  self_offset[16] = "0";  // AO-encoded "x&y"
    int   immediate    = 0;
    int   looping_sfx  = 0;
    int   screenshake  = 0;
    char  frame_ss[8]  = "0";
    char  frame_real[8]= "0";
    char  frame_sfx[8] = "0";
    int   additive     = 0;
    char  effects[16]  = "0";
};

inline int ms(char* buf, int cap, const MSParams& p) {
    // Escape all string fields
    char e_pre[128], e_chr[128], e_emo[128], e_msg[1024],
         e_pos[32],  e_sfx[128], e_sho[128], e_off[32], e_eff[32];
    Packet::escape(p.pre_anim,   e_pre, sizeof(e_pre));
    Packet::escape(p.char_name,  e_chr, sizeof(e_chr));
    Packet::escape(p.emote,      e_emo, sizeof(e_emo));
    Packet::escape(p.message,    e_msg, sizeof(e_msg));
    Packet::escape(p.pos,        e_pos, sizeof(e_pos));
    Packet::escape(p.sfx,        e_sfx, sizeof(e_sfx));
    Packet::escape(p.showname,   e_sho, sizeof(e_sho));
    Packet::escape(p.self_offset,e_off, sizeof(e_off));
    Packet::escape(p.effects,    e_eff, sizeof(e_eff));

    return std::snprintf(buf, cap,
        "MS#%d#%s#%s#%s#%s#%s#%s#%d#%d#%d#%d#%d#%d#%d#%d#%s#%d#%s#%d#%d#%d#%s#%s#%s#%d#%s#%%",
        p.desk_mod, e_pre, e_chr, e_emo, e_msg, e_pos, e_sfx,
        p.emote_mod, p.char_id, p.clip, p.objection, p.evidence_id,
        p.flip, p.realization, p.text_color, e_sho,
        p.other_charid, e_off, p.immediate, p.looping_sfx, p.screenshake,
        p.frame_ss, p.frame_real, p.frame_sfx, p.additive, e_eff);
}

} // namespace cmd
} // namespace ao
