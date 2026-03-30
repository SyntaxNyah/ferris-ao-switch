#include "ao_client.hpp"
#include "commands.hpp"
#include <cstdio>
#include <cstring>
#include <cstdarg>

namespace ao {

AOClient::AOClient(OutQueue& out, GameState& state, const char* username)
    : out_(out), state_(state) {
    std::strncpy(username_, username, sizeof(username_) - 1);
}

void AOClient::on_connected(const char* hdid) {
    std::strncpy(hdid_, hdid, sizeof(hdid_) - 1);
    hs_state_ = HandshakeState::WaitDecryptor;
    parse_len_ = 0;
    // Server sends decryptor first — we just wait.
}

void AOClient::on_disconnected() {
    hs_state_ = HandshakeState::Idle;
    state_.connected  = false;
    state_.in_lobby   = false;
    parse_len_ = 0;
}

void AOClient::send(const char* buf, int len) {
    OutPacket pkt;
    int copy = len < (int)sizeof(pkt.data) - 1 ? len : (int)sizeof(pkt.data) - 1;
    std::memcpy(pkt.data, buf, copy);
    pkt.data[copy] = '\0';
    pkt.len = copy;
    out_.push(pkt);
}

void AOClient::send_fmt(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) send(buf, n);
}

void AOClient::process(InQueue& in) {
    InPacket raw;
    while (in.pop(raw)) {
        // Synthetic disconnect signal from network thread
        if (std::strcmp(raw.data, "__DISCONNECT#%") == 0) {
            on_disconnected();
            continue;
        }

        // Append to parse buffer
        int space = (int)sizeof(parse_buf_) - parse_len_ - 1;
        int copy  = raw.len < space ? raw.len : space;
        std::memcpy(parse_buf_ + parse_len_, raw.data, copy);
        parse_len_ += copy;
        parse_buf_[parse_len_] = '\0';

        // Extract and handle complete packets
        int consumed = 0;
        while (consumed < parse_len_) {
            Packet pkt;
            int n = parse_packet(parse_buf_ + consumed, parse_len_ - consumed, pkt);
            if (n == 0) break;
            consumed += n;
            handle(pkt);
        }
        if (consumed > 0) {
            std::memmove(parse_buf_, parse_buf_ + consumed, parse_len_ - consumed);
            parse_len_ -= consumed;
            parse_buf_[parse_len_] = '\0';
        }
    }
}

void AOClient::handle(const Packet& pkt) {
    const char* h = pkt.header;

    // Synthetic / internal
    if (std::strcmp(h, "__DISCONNECT") == 0) { on_disconnected(); return; }

    // Handshake sequence
    if (std::strcmp(h, "decryptor")   == 0) { on_decryptor(pkt);  return; }
    if (std::strcmp(h, "ID")          == 0) { on_id(pkt);         return; }
    if (std::strcmp(h, "PN")          == 0) { on_pn(pkt);         return; }
    if (std::strcmp(h, "FL")          == 0) { on_fl(pkt);         return; }
    if (std::strcmp(h, "ASS")         == 0) { /* asset URL — store if needed */ return; }
    if (std::strcmp(h, "SI")          == 0) { on_si(pkt);         return; }
    if (std::strcmp(h, "SC")          == 0) { on_sc(pkt);         return; }
    if (std::strcmp(h, "SM")          == 0) { on_sm(pkt);         return; }
    if (std::strcmp(h, "DONE")        == 0) { on_done(pkt);       return; }

    // In-lobby
    if (std::strcmp(h, "MS")          == 0) { on_ms(pkt);         return; }
    if (std::strcmp(h, "CT")          == 0) { on_ct(pkt);         return; }
    if (std::strcmp(h, "ARUP")        == 0) { on_arup(pkt);       return; }
    if (std::strcmp(h, "MC")          == 0) { on_mc(pkt);         return; }
    if (std::strcmp(h, "HP")          == 0) { on_hp(pkt);         return; }
    if (std::strcmp(h, "BN")          == 0) { on_bn(pkt);         return; }
    if (std::strcmp(h, "LE")          == 0) { on_le(pkt);         return; }
    if (std::strcmp(h, "CharsCheck")  == 0) { on_chars_check(pkt);return; }
    if (std::strcmp(h, "PV")          == 0) { on_pv(pkt);         return; }
    if (std::strcmp(h, "AUTH")        == 0) { on_auth(pkt);       return; }
    if (std::strcmp(h, "BD")          == 0) { on_bd(pkt);         return; }
    if (std::strcmp(h, "ZZ")          == 0) { on_zz(pkt);         return; }
    if (std::strcmp(h, "CHECK")       == 0) { on_check(pkt);      return; }
    if (std::strcmp(h, "CASEA")       == 0) { /* TODO */ return; }

    std::fprintf(stderr, "[ao_client] unknown packet: %s\n", h);
}

// ── Handshake handlers ─────────────────────────────────────────────────────────

void AOClient::on_decryptor(const Packet& /*p*/) {
    // Server says NOENCRYPT — send HI
    char buf[256];
    int n = cmd::hi(buf, sizeof(buf), hdid_);
    send(buf, n);
    hs_state_ = HandshakeState::WaitId;
}

void AOClient::on_id(const Packet& p) {
    // Store server name/version
    std::strncpy(state_.server_name,    p.field(1), sizeof(state_.server_name) - 1);
    std::strncpy(state_.server_version, p.field(2), sizeof(state_.server_version) - 1);

    if (hs_state_ == HandshakeState::WaitId) {
        // Send client ID
        char buf[128];
        int n = cmd::id(buf, sizeof(buf));
        send(buf, n);
        hs_state_ = HandshakeState::WaitSi;
        // Server will now send PN + FL [+ ASS], then wait for askchaa
    }
}

void AOClient::on_pn(const Packet& /*p*/) {
    // Player count info — send askchaa now
    if (hs_state_ == HandshakeState::WaitSi) {
        char buf[32];
        send(buf, cmd::askchaa(buf, sizeof(buf)));
    }
}

void AOClient::on_fl(const Packet& /*p*/) {
    // Feature flags — nothing to store for now (we support everything)
}

void AOClient::on_si(const Packet& p) {
    // SI#char_count#evi_count#music_count#%
    // Store counts (informational), then request character list
    (void)p;
    if (hs_state_ == HandshakeState::WaitSi) {
        char buf[32];
        send(buf, cmd::rc(buf, sizeof(buf)));
        hs_state_ = HandshakeState::WaitSc;
    }
}

void AOClient::on_sc(const Packet& p) {
    // SC#char0#char1#...#%
    state_.char_count = 0;
    for (int i = 0; i < p.field_count && i < GameState::MAX_CHARS; ++i) {
        char tmp[64];
        std::strncpy(tmp, p.field(i), sizeof(tmp) - 1);
        Packet::unescape(tmp);
        std::strncpy(state_.characters[i].name, tmp, sizeof(state_.characters[0].name) - 1);
        state_.characters[i].showname[0] = '\0';
        ++state_.char_count;
    }
    if (hs_state_ == HandshakeState::WaitSc) {
        char buf[32];
        send(buf, cmd::rm(buf, sizeof(buf)));
        hs_state_ = HandshakeState::WaitSm;
    }
}

void AOClient::on_sm(const Packet& p) {
    // SM#area0#area1#...#song0#song1#...#%
    // Areas come first (no '.'), then music files (contain '.')
    state_.area_count  = 0;
    state_.music_count = 0;
    bool in_music = false;

    for (int i = 0; i < p.field_count; ++i) {
        const char* f = p.field(i);
        if (!in_music && std::strchr(f, '.') != nullptr) in_music = true;

        if (!in_music) {
            if (state_.area_count < GameState::MAX_AREAS) {
                char tmp[128];
                std::strncpy(tmp, f, sizeof(tmp) - 1);
                Packet::unescape(tmp);
                std::strncpy(state_.areas[state_.area_count].name, tmp,
                    sizeof(state_.areas[0].name) - 1);
                ++state_.area_count;
            }
        } else {
            if (state_.music_count < GameState::MAX_MUSIC) {
                char tmp[128];
                std::strncpy(tmp, f, sizeof(tmp) - 1);
                Packet::unescape(tmp);
                std::strncpy(state_.music_list[state_.music_count], tmp,
                    sizeof(state_.music_list[0]) - 1);
                ++state_.music_count;
            }
        }
    }

    if (hs_state_ == HandshakeState::WaitSm) {
        char buf[32];
        send(buf, cmd::rd(buf, sizeof(buf)));
        hs_state_ = HandshakeState::WaitDone;
    }
}

void AOClient::on_done(const Packet& /*p*/) {
    state_.in_lobby  = true;
    state_.connected = true;
    hs_state_ = HandshakeState::InLobby;
    std::fprintf(stdout, "[ao_client] Handshake complete — in lobby\n");
}

// ── In-lobby handlers ──────────────────────────────────────────────────────────

void AOClient::on_ms(const Packet& p) {
    // 30 server-broadcast fields
    ICAnimState& ic = state_.ic_anim;

    auto copy_field = [&](char* dst, int dst_size, int idx) {
        char tmp[Packet::MAX_FIELD_LEN];
        std::strncpy(tmp, p.field(idx), sizeof(tmp) - 1);
        Packet::unescape(tmp);
        std::strncpy(dst, tmp, dst_size - 1);
        dst[dst_size - 1] = '\0';
    };

    // [0] desk_mod  [1] pre_anim  [2] char_name  [3] emote
    ic.emote_mod  = p.field_int(0);
    copy_field(ic.pre_anim,   sizeof(ic.pre_anim),  1);
    copy_field(ic.char_name,  sizeof(ic.char_name),  2);
    copy_field(ic.emote,      sizeof(ic.emote),      3);
    // [4] message
    copy_field(ic.message,    sizeof(ic.message),    4);
    // [5] pos  [6] sfx  [7] emote_mod  [8] char_id
    copy_field(ic.pos,        sizeof(ic.pos),        5);
    copy_field(ic.sfx,        sizeof(ic.sfx),        6);
    ic.emote_mod    = p.field_int(7);
    ic.char_id      = p.field_int(8);
    // [10] objection  [11] evidence  [12] flip  [13] realization  [14] text_color
    ic.objection_mod = p.field_int(10);
    ic.evidence_id   = p.field_int(11);
    ic.flip          = p.field_int(12) != 0;
    ic.realization   = p.field_int(13) != 0;
    ic.text_color    = p.field_int(14);
    // [15] showname  [16] other_charid
    copy_field(ic.showname,   sizeof(ic.showname),  15);
    ic.other_charid = p.field_int(16);
    // [17] other_name  [18] other_emote
    copy_field(ic.other_emote, sizeof(ic.other_emote), 18);
    // [19] self_offset  [20] other_offset  [21] other_flip
    ic.self_offset  = p.field_int(19);
    ic.other_offset = p.field_int(20);
    ic.other_flip   = p.field_int(21) != 0;
    // [22] immediate  [23] looping_sfx  [24] screenshake
    ic.immediate    = p.field_int(22) != 0;
    ic.looping_sfx  = p.field_int(23) != 0;
    ic.screenshake  = p.field_int(24) != 0;
    // [28] additive
    ic.additive     = p.field_int(28) != 0;

    // Push to IC chat log
    ChatEntry entry;
    std::strncpy(entry.name,
        ic.showname[0] ? ic.showname : ic.char_name,
        sizeof(entry.name) - 1);
    std::strncpy(entry.message, ic.message, sizeof(entry.message) - 1);
    entry.color  = (uint8_t)ic.text_color;
    entry.server = false;
    state_.ic_log.push(entry);

    ic.pending = true;
}

void AOClient::on_ct(const Packet& p) {
    // CT#username#message#is_server#%
    ChatEntry entry;
    char name[64], msg[512];
    std::strncpy(name, p.field(0), sizeof(name) - 1);
    std::strncpy(msg,  p.field(1), sizeof(msg)  - 1);
    Packet::unescape(name);
    Packet::unescape(msg);
    std::strncpy(entry.name,    name, sizeof(entry.name)    - 1);
    std::strncpy(entry.message, msg,  sizeof(entry.message) - 1);
    entry.color  = 0;
    entry.server = p.field_int(2) != 0;
    state_.ooc_log.push(entry);
}

void AOClient::on_arup(const Packet& p) {
    // ARUP#type#val0#val1#...#%
    int type = p.field_int(0);
    for (int i = 1; i < p.field_count && (i - 1) < state_.area_count; ++i) {
        int idx = i - 1;
        if (idx >= GameState::MAX_AREAS) break;
        const char* val = p.field(i);
        switch (type) {
            case 0: state_.areas[idx].players    = std::atoi(val); break;
            case 1: std::strncpy(state_.areas[idx].status,     val, 31); break;
            case 2: std::strncpy(state_.areas[idx].cm_label,   val, 63); break;
            case 3: std::strncpy(state_.areas[idx].lock_state, val, 15); break;
        }
    }
}

void AOClient::on_mc(const Packet& p) {
    // MC#song#char_id#name#...#%
    char song[128];
    std::strncpy(song, p.field(0), sizeof(song) - 1);
    Packet::unescape(song);
    std::strncpy(state_.current_music, song, sizeof(state_.current_music) - 1);
    // TODO Phase 8: tell audio manager to play the song
}

void AOClient::on_hp(const Packet& p) {
    int bar = p.field_int(0);
    int val = p.field_int(1);
    if (val < 0) val = 0;
    if (val > 10) val = 10;
    if (bar == 1) state_.hp_defense     = val;
    if (bar == 2) state_.hp_prosecution = val;
}

void AOClient::on_bn(const Packet& p) {
    char bg[128];
    std::strncpy(bg, p.field(0), sizeof(bg) - 1);
    Packet::unescape(bg);
    std::strncpy(state_.background, bg, sizeof(state_.background) - 1);
}

void AOClient::on_le(const Packet& p) {
    state_.evidence_count = 0;
    for (int i = 0; i < p.field_count && i < GameState::MAX_EVIDENCE; ++i) {
        char entry[700];
        std::strncpy(entry, p.field(i), sizeof(entry) - 1);
        Packet::unescape(entry);
        // Entry format: "NAME&DESCRIPTION&IMAGE"
        char* sep1 = std::strchr(entry, '&');
        if (!sep1) continue;
        *sep1 = '\0';
        char* sep2 = std::strchr(sep1 + 1, '&');
        if (!sep2) continue;
        *sep2 = '\0';

        EvidenceEntry& ev = state_.evidence[state_.evidence_count];
        std::strncpy(ev.name,        entry,      sizeof(ev.name)        - 1);
        std::strncpy(ev.description, sep1 + 1,   sizeof(ev.description) - 1);
        std::strncpy(ev.image,       sep2 + 1,   sizeof(ev.image)       - 1);
        ++state_.evidence_count;
    }
}

void AOClient::on_chars_check(const Packet& p) {
    for (int i = 0; i < p.field_count && i < GameState::MAX_CHARS; ++i)
        state_.char_taken[i] = (p.field(i)[0] == '1');
}

void AOClient::on_pv(const Packet& p) {
    // PV#0#CID#char_id#%
    if (p.field_count >= 3 && std::strcmp(p.field(1), "CID") == 0)
        state_.my_char_id = p.field_int(2);
}

void AOClient::on_auth(const Packet& p) {
    const char* val = p.field(0);
    if (std::strcmp(val, "-1") == 0) {
        state_.authenticated = false;
        state_.mod_name[0]   = '\0';
    } else {
        state_.authenticated = true;
        std::strncpy(state_.mod_name, val, sizeof(state_.mod_name) - 1);
    }
}

void AOClient::on_bd(const Packet& p) {
    // Server kicked/banned us
    std::fprintf(stderr, "[ao_client] BD: %s\n", p.field(0));
    // Trigger disconnect
    on_disconnected();
}

void AOClient::on_zz(const Packet& p) {
    // Mod call notification — show as server OOC message
    ChatEntry entry;
    std::strncpy(entry.name,    "SERVER", sizeof(entry.name)    - 1);
    std::strncpy(entry.message, p.field(0), sizeof(entry.message) - 1);
    entry.color  = 2; // red
    entry.server = true;
    state_.ooc_log.push(entry);
}

void AOClient::on_check(const Packet& /*p*/) {
    // Server responded to our CH ping — nothing to do
}

} // namespace ao
