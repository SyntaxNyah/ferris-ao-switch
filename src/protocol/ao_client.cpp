#include "ao_client.hpp"
#include "commands.hpp"
#include "../assets/asset_manager.hpp"
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
    hs_state_   = HandshakeState::WaitDecryptor;
    parse_len_  = 0;
    hs_start_ms_ = SDL_GetTicks();

    // AO-SDL sends nothing on connect — HI is emitted in response to the
    // decryptor packet (see AO-SDL plugins/net/ao/PacketBehavior.cpp,
    // AOPacketDecryptor::handle). Every AO2 server sends decryptor as its
    // first line, so waiting for it is the correct gate.
    std::fprintf(stderr, "[ao_client] connected — waiting for decryptor (AO-SDL flow)\n");
}

void AOClient::on_connected_ws(const char* hdid) {
    // AO2 — including webAO — sends HI *in response to* the server's `decryptor`
    // packet, which is always the server's first message (see LemmyAO/webAO:
    // "decryptor (s2c) — server's first packet"; "HI — the first packet a client
    // sends after the server's decryptor"). The old code fired HI eagerly on WS
    // open AND re-sent it on decryptor, so akashi received HI twice and closed
    // the socket. Wait for decryptor and send HI exactly once — identical to the
    // raw-TCP path.
    on_connected(hdid);
}

void AOClient::on_disconnected() {
    hs_state_        = HandshakeState::Idle;
    state_.connected = false;
    state_.in_lobby  = false;
    parse_len_       = 0;
    // Asset URL lifecycle is managed by App, not here.
    // App::disconnect() / App::connect() call set/clear_asset_url as needed.
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
    // ── Drain the incoming queue FIRST ────────────────────────────────────────
    // Drain before checking the global timeout so any queued handshake packet
    // can transition us to InLobby before the 60s clock fires. Blocking HTTP
    // fetches in App::update() can stall the main thread 2-4 s; draining first
    // ensures queued packets are never discarded by stale timers.
    static InPacket raw; // static: 128 KB on the call stack is too large
    while (in.pop(raw)) {
        if (std::strcmp(raw.data, "__DISCONNECT#%") == 0) {
            on_disconnected();
            continue;
        }
        int space = (int)sizeof(parse_buf_) - parse_len_ - 1;
        int copy  = raw.len < space ? raw.len : space;
        std::memcpy(parse_buf_ + parse_len_, raw.data, copy);
        parse_len_ += copy;
        parse_buf_[parse_len_] = '\0';

        int consumed = 0;
        while (consumed < parse_len_) {
            Packet pkt;
            int n = parse_packet(parse_buf_ + consumed, parse_len_ - consumed, pkt);
            if (n == 0) break;
            // SC / SM / CharsCheck can exceed Packet::MAX_FIELDS on servers
            // with hundreds of characters or music tracks — dispatch them to
            // streaming handlers that walk the raw bytes directly.
            if (std::strcmp(pkt.header, "SC") == 0) {
                on_sc_stream(parse_buf_ + consumed, n);
            } else if (std::strcmp(pkt.header, "SM") == 0) {
                on_sm_stream(parse_buf_ + consumed, n);
            } else if (std::strcmp(pkt.header, "CharsCheck") == 0) {
                on_chars_check_stream(parse_buf_ + consumed, n);
            } else {
                handle(pkt);
            }
            consumed += n;
        }
        if (consumed > 0) {
            std::memmove(parse_buf_, parse_buf_ + consumed, parse_len_ - consumed);
            parse_len_ -= consumed;
            parse_buf_[parse_len_] = '\0';
        }
    }

    // ── Global handshake timeout (checked AFTER drain so resets take effect) ──
    // AO-SDL's handshake is entirely reactive — each packet triggers the
    // next send. If nothing happens for 60s the server is dead; drop the
    // connection. on_decryptor() resets hs_start_ms_ so the budget starts
    // from the real handshake begin, not from TCP/TLS setup.
    if (hs_state_ != HandshakeState::Idle &&
        hs_state_ != HandshakeState::InLobby &&
        hs_start_ms_ != 0 &&
        (int32_t)(SDL_GetTicks() - hs_start_ms_) > 60000) {
        std::fprintf(stderr, "[ao_client] handshake timed out (state=%d)\n",
            (int)hs_state_);
        on_disconnected();
        static InPacket disc;
        std::strncpy(disc.data, "__DISCONNECT#%", sizeof(disc.data));
        disc.len = (int)std::strlen(disc.data);
        in.push(disc);
        return;
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
    if (std::strcmp(h, "ASS")         == 0) { on_ass(pkt);         return; }
    if (std::strcmp(h, "SI")          == 0) { on_si(pkt);         return; }
    // SC and SM are streaming-dispatched from process() — never reach here.
    if (std::strcmp(h, "DONE")        == 0) { on_done(pkt);       return; }

    // In-lobby
    if (std::strcmp(h, "MS")          == 0) { on_ms(pkt);         return; }
    if (std::strcmp(h, "CT")          == 0) { on_ct(pkt);         return; }
    if (std::strcmp(h, "ARUP")        == 0) { on_arup(pkt);       return; }
    if (std::strcmp(h, "MC")          == 0) { on_mc(pkt);         return; }
    if (std::strcmp(h, "HP")          == 0) { on_hp(pkt);         return; }
    if (std::strcmp(h, "BN")          == 0) { on_bn(pkt);         return; }
    if (std::strcmp(h, "LE")          == 0) { on_le(pkt);         return; }
    // CharsCheck is streaming-dispatched from process() — never reach here.
    if (std::strcmp(h, "PV")          == 0) { on_pv(pkt);         return; }
    if (std::strcmp(h, "AUTH")        == 0) { on_auth(pkt);       return; }
    if (std::strcmp(h, "BD")          == 0) { on_bd(pkt);         return; }
    if (std::strcmp(h, "ZZ")          == 0) { on_zz(pkt);         return; }
    if (std::strcmp(h, "CHECK")       == 0) { on_check(pkt);      return; }
    if (std::strcmp(h, "CASEA")       == 0) { /* TODO */ return; }

    // Akashi-specific
    if (std::strcmp(h, "FA")          == 0) { on_fa(pkt);         return; }
    if (std::strcmp(h, "PR")          == 0) { on_pr(pkt);         return; }
    if (std::strcmp(h, "PU")          == 0) { on_pu(pkt);         return; }
    if (std::strcmp(h, "TI")          == 0) { on_ti(pkt);         return; }
    if (std::strcmp(h, "SP")          == 0) { return; } // area/position update, ignore

    std::fprintf(stderr, "[ao_client] unknown packet: %s\n", h);
}

// ── Handshake handlers ─────────────────────────────────────────────────────────

void AOClient::on_decryptor(const Packet& /*p*/) {
    // AO-SDL sends HI in response to decryptor (see AO-SDL PacketBehavior.cpp,
    // AOPacketDecryptor::handle). We don't use the decryptor key itself —
    // modern servers ship "NOENCRYPT" and clients ignore the value. The
    // packet's sole purpose is to signal "you may begin the handshake now".

    if (hs_state_ == HandshakeState::WaitDecryptor) {
        // AO-SDL / raw TCP flow: first time seeing decryptor, send HI.
        hs_start_ms_ = SDL_GetTicks();
        char buf[256];
        send(buf, cmd::hi(buf, sizeof(buf), hdid_));
        hs_state_ = HandshakeState::WaitId;
        std::fprintf(stderr, "[ao_client] decryptor received — sent HI (AO-SDL flow)\n");
        return;
    }

    if (hs_state_ == HandshakeState::WaitId && !hi_resent_) {
        // webAO fallback for vanilla tsuserver: we already sent HI eagerly
        // on WS open (on_connected_ws), but the server hasn't responded with
        // ID yet.  Vanilla tsuserver ignores HI that arrives before its own
        // decryptor broadcast — the arrival of decryptor while we're still
        // in WaitId is strong evidence our early HI was dropped.  Re-send
        // HI now that the server has signalled readiness.  The flag prevents
        // compounding duplicates if the server sends multiple decryptors.
        hi_resent_ = true;
        hs_start_ms_ = SDL_GetTicks();
        char buf[256];
        send(buf, cmd::hi(buf, sizeof(buf), hdid_));
        std::fprintf(stderr, "[ao_client] decryptor in WaitId — re-sent HI (vanilla fallback)\n");
        return;
    }

    // Past WaitId (WaitSi, WaitSc, InLobby, etc.) — server already
    // processed our HI, ignore the late decryptor.
    std::fprintf(stderr, "[ao_client] decryptor received (state=%d) — ignoring\n",
        (int)hs_state_);
}

void AOClient::on_id(const Packet& p) {
    // Server's ID packet: "ID#<uid>#<software>#<version>#%"
    state_.my_uid = p.field_int(0);
    std::strncpy(state_.server_name,    p.field(1), sizeof(state_.server_name) - 1);
    std::strncpy(state_.server_version, p.field(2), sizeof(state_.server_version) - 1);

    // AO-SDL replies with ID#AO-SDL/<version>#2.999.999#% — the fake
    // "2.999.999" protocol version makes Akashi treat us as a fully modern
    // AO2 client with all features enabled. Match that format verbatim (keep the
    // 2.999.999 protocol field; only the software name carries our version).
    char buf[128];
    send(buf, cmd::id(buf, sizeof(buf), "ferris-ao-switchv0.5", "2.999.999"));
    std::fprintf(stderr, "[ao_client] ID received — sent client ID\n");
}

void AOClient::on_pn(const Packet& /*p*/) {
    // AO-SDL's AOPacketPN::handle sends askchaa in response to the player-
    // count packet. That is the trigger for the server to emit SI next.
    char buf[32];
    send(buf, cmd::askchaa(buf, sizeof(buf)));
    if (hs_state_ == HandshakeState::WaitId)
        hs_state_ = HandshakeState::WaitSi;
    std::fprintf(stderr, "[ao_client] PN received — sent askchaa\n");
}

void AOClient::on_fl(const Packet& /*p*/) {
    // Feature flags — nothing to store for now (we support everything)
}

void AOClient::on_ass(const Packet& p) {
    // ASS#<base_url>#%  — server-provided HTTP asset base URL (optional)
    // If present, all asset lookups try this URL first before local base / romfs.
    const char* url = p.field(0);
    if (url && url[0] != '\0') {
        AssetManager::set_asset_url(url);
        std::fprintf(stderr, "[ao_client] ASS asset URL: %s\n", url);
    } else {
        AssetManager::clear_asset_url();
    }
}

void AOClient::on_si(const Packet& p) {
    // SI#char_count#evi_count#music_count#%
    // AO-SDL's AOPacketSI::handle unconditionally sends RC. Match that.
    (void)p;
    char buf[32];
    send(buf, cmd::rc(buf, sizeof(buf)));
    if (hs_state_ == HandshakeState::WaitId || hs_state_ == HandshakeState::WaitSi)
        hs_state_ = HandshakeState::WaitSc;
    std::fprintf(stderr, "[ao_client] SI received — sent RC\n");
}

void AOClient::on_sc_stream(const char* raw, int len) {
    // SC#char0#char1#...#%
    // Each field is "Name&desc&&&&&" per the AO2 wire format. webAO splits
    // on '&' and takes chargs[0] as the folder name; we do the same. The raw
    // streaming iterator is used instead of Packet because SC on large
    // servers (e.g. ao.umineko.online) can contain 600+ fields, which would
    // silently truncate at Packet::MAX_FIELDS = 35.
    state_.char_count = 0;
    stream_packet_fields(raw, len, [&](const char* f, int flen) {
        if (state_.char_count >= GameState::MAX_CHARS) return;
        // Take the substring before the first '&' as the folder name
        int name_len = 0;
        while (name_len < flen && f[name_len] != '&') ++name_len;
        constexpr int NAME_CAP = (int)sizeof(state_.characters[0].name);
        char tmp[NAME_CAP * 4]; // unescape is shrink-only, so a little headroom is enough
        int copy = name_len < (int)sizeof(tmp) - 1 ? name_len : (int)sizeof(tmp) - 1;
        std::memcpy(tmp, f, copy);
        tmp[copy] = '\0';
        Packet::unescape(tmp);
        std::strncpy(state_.characters[state_.char_count].name, tmp, NAME_CAP - 1);
        state_.characters[state_.char_count].name[NAME_CAP - 1] = '\0';
        state_.characters[state_.char_count].showname[0] = '\0';
        ++state_.char_count;
    });
    std::fprintf(stderr, "[ao_client] SC: %d characters (streamed)\n", state_.char_count);
    // AO-SDL's AOPacketSC::handle unconditionally sends RM. Match that.
    char buf[32];
    send(buf, cmd::rm(buf, sizeof(buf)));
    if (hs_state_ != HandshakeState::InLobby)
        hs_state_ = HandshakeState::WaitSm;
    std::fprintf(stderr, "[ao_client] SC processed — sent RM\n");
}

void AOClient::on_sm_stream(const char* raw, int len) {
    // SM#area0#area1#...#song0#song1#...#%
    // Areas come first (no '.'), then music files (contain '.').
    // Streamed for the same reason as on_sc_stream — the music list can
    // easily exceed Packet::MAX_FIELDS = 35 on music-heavy servers.
    state_.area_count  = 0;
    state_.music_count = 0;
    bool in_music = false;

    stream_packet_fields(raw, len, [&](const char* f, int flen) {
        // "In music" once we see a filename with a dot. Category dividers
        // like "=== songs ===" without a dot stay treated as areas on some
        // servers — webAO's logic matches this heuristic.
        if (!in_music) {
            // Scan for '.' in [0..flen)
            for (int i = 0; i < flen; ++i) {
                if (f[i] == '.') { in_music = true; break; }
            }
        }
        if (!in_music) {
            if (state_.area_count < GameState::MAX_AREAS) {
                constexpr int CAP = (int)sizeof(state_.areas[0].name);
                char tmp[CAP * 4];
                int copy = flen < (int)sizeof(tmp) - 1 ? flen : (int)sizeof(tmp) - 1;
                std::memcpy(tmp, f, copy);
                tmp[copy] = '\0';
                Packet::unescape(tmp);
                std::strncpy(state_.areas[state_.area_count].name, tmp, CAP - 1);
                state_.areas[state_.area_count].name[CAP - 1] = '\0';
                ++state_.area_count;
            }
        } else {
            if (state_.music_count < GameState::MAX_MUSIC) {
                constexpr int CAP = (int)sizeof(state_.music_list[0]);
                char tmp[CAP * 4];
                int copy = flen < (int)sizeof(tmp) - 1 ? flen : (int)sizeof(tmp) - 1;
                std::memcpy(tmp, f, copy);
                tmp[copy] = '\0';
                Packet::unescape(tmp);
                std::strncpy(state_.music_list[state_.music_count], tmp, CAP - 1);
                state_.music_list[state_.music_count][CAP - 1] = '\0';
                ++state_.music_count;
            }
        }
    });
    std::fprintf(stderr, "[ao_client] SM: %d areas, %d music (streamed)\n",
        state_.area_count, state_.music_count);

    // AO-SDL's AOPacketSM::handle unconditionally sends RD. Match that.
    char buf[32];
    send(buf, cmd::rd(buf, sizeof(buf)));
    if (hs_state_ != HandshakeState::InLobby)
        hs_state_ = HandshakeState::WaitDone;
    std::fprintf(stderr, "[ao_client] SM processed — sent RD\n");
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
    ic.desk_mod   = p.field_int(0, 1);
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
    // CourtroomScreen watches state_.current_music and calls MusicPlayer::play()
    // when it changes — AOClient has no audio access and stays main-thread-pure.
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

void AOClient::on_chars_check_stream(const char* raw, int len) {
    // CharsCheck#0#1#0#...#% — one field per character, '1' if taken.
    // Streamed because field count == char_count, which can be hundreds.
    int idx = 0;
    stream_packet_fields(raw, len, [&](const char* f, int /*flen*/) {
        if (idx >= GameState::MAX_CHARS) return;
        state_.char_taken[idx] = (f[0] == '1');
        ++idx;
    });
    // When SC never arrived, use CharsCheck field count as char_count so
    // CharSelectScreen has slots to display.
    if (state_.char_count == 0)
        state_.char_count = idx;
    // CharsCheck is a broadcast — never trigger handshake actions from here.
    // The handshake is driven by on_decryptor/on_id/on_si and their timers.
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
    // Server is pinging us — respond so we aren't disconnected for inactivity
    char buf[16];
    send(buf, cmd::ch(buf, sizeof(buf)));
}

// ── Akashi-specific handlers ───────────────────────────────────────────────────

void AOClient::on_fa(const Packet& p) {
    // FA#area0#area1#...#% — full area list, sent by Akashi instead of putting
    // areas in SM. Overwrite whatever SM might have set.
    state_.area_count = 0;
    for (int i = 0; i < p.field_count && i < GameState::MAX_AREAS; ++i) {
        char tmp[128];
        std::strncpy(tmp, p.field(i), sizeof(tmp) - 1);
        Packet::unescape(tmp);
        std::strncpy(state_.areas[state_.area_count].name, tmp,
            sizeof(state_.areas[0].name) - 1);
        ++state_.area_count;
    }
    std::fprintf(stderr, "[ao_client] FA: %d areas\n", state_.area_count);
}

void AOClient::on_pr(const Packet& /*p*/) {
    // PR#uid#type#% — player roster add/remove broadcast. Informational only
    // in AO-SDL (AOPacketPR::handle publishes a PlayerListEvent). We don't
    // maintain a roster yet, so discard.
}

void AOClient::on_pu(const Packet& /*p*/) {
    // PU#uid#data_type#value#% — player state update. Informational only in
    // AO-SDL (AOPacketPU::handle publishes a PlayerListEvent). Discard.
}

void AOClient::on_ti(const Packet& /*p*/) {
    // TI#timer_id#type#value#% — area timer update. Not displayed yet.
}

} // namespace ao
