#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace ao {

// ── AO2 packet ────────────────────────────────────────────────────────────────
// Wire format:  HEADER#field0#field1#...#%
// All fields use fixed-size char arrays — no heap allocation.

struct Packet {
    static constexpr int MAX_FIELDS    = 35;
    static constexpr int MAX_FIELD_LEN = 2048;
    static constexpr int MAX_HEADER    = 32;

    char    header[MAX_HEADER];
    char    fields[MAX_FIELDS][MAX_FIELD_LEN];
    int     field_count = 0;

    const char* field(int i) const {
        if (i < 0 || i >= field_count) return "";
        return fields[i];
    }

    int field_int(int i, int def = 0) const {
        const char* s = field(i);
        if (!s || !*s) return def;
        return std::atoi(s);
    }

    // Unescape a single field in-place.
    // AO2 escapes: <num>→#  <percent>→%  <dollar>→$  <and>→&
    static void unescape(char* s) {
        char buf[MAX_FIELD_LEN];
        int r = 0, w = 0;
        int len = (int)std::strlen(s);
        while (r < len) {
            if (s[r] == '<') {
                if (std::strncmp(s + r, "<num>",     5) == 0) { buf[w++]='#'; r+=5; continue; }
                if (std::strncmp(s + r, "<percent>", 9) == 0) { buf[w++]='%'; r+=9; continue; }
                if (std::strncmp(s + r, "<dollar>",  8) == 0) { buf[w++]='$'; r+=8; continue; }
                if (std::strncmp(s + r, "<and>",     5) == 0) { buf[w++]='&'; r+=5; continue; }
            }
            buf[w++] = s[r++];
        }
        buf[w] = '\0';
        // Copy exactly w+1 bytes — unescaping only shrinks the string so
        // w <= strlen(input), making this safe for any caller buffer size.
        std::memcpy(s, buf, w + 1);
    }

    // Escape src into dst (dst must be >= strlen(src)*9+1 bytes in worst case).
    static void escape(const char* src, char* dst, int dst_cap) {
        int w = 0;
        for (const char* p = src; *p && w < dst_cap - 10; ++p) {
            switch (*p) {
                case '#': if (w+5<dst_cap){std::memcpy(dst+w,"<num>",5);    w+=5;} break;
                case '%': if (w+9<dst_cap){std::memcpy(dst+w,"<percent>",9);w+=9;} break;
                case '$': if (w+8<dst_cap){std::memcpy(dst+w,"<dollar>",8); w+=8;} break;
                case '&': if (w+5<dst_cap){std::memcpy(dst+w,"<and>",5);    w+=5;} break;
                default:  dst[w++] = *p; break;
            }
        }
        dst[w] = '\0';
    }
};

// Stream-iterate the fields of one raw packet without copying into Packet.
// `data` points at the start of the packet (header) and `len` is at least
// enough bytes to reach the `%` terminator — i.e. the byte count returned by
// parse_packet for this same packet. The callback `cb(field_ptr, field_len)`
// is invoked once per `#`-separated field AFTER the header, stopping at the
// `%` terminator. Intended for packets whose field count can exceed
// Packet::MAX_FIELDS (SC, SM, CharsCheck on servers with large rosters).
//
// The bytes handed to `cb` point into `data` and are NOT null-terminated —
// the callback must copy them into its own buffer before using them as a
// C string. `cb` must be a callable with signature void(const char*, int).
template <typename Fn>
inline void stream_packet_fields(const char* data, int len, Fn&& cb) {
    // Skip the header (first '#')
    int i = 0;
    while (i < len && data[i] != '#' && data[i] != '%') ++i;
    if (i >= len || data[i] == '%') return;
    ++i; // past first '#'

    int seg_start = i;
    for (; i < len; ++i) {
        char c = data[i];
        if (c == '%') {
            // End of packet. Emit the pending segment only if it's not the
            // lone '%' terminator (seg_start points right at '%' in that case).
            break;
        }
        if (c == '#') {
            int seg_len = i - seg_start;
            // Guard: a lone `%` field would be the terminator token, never a
            // real field. parse_packet special-cases it — we do the same.
            if (!(seg_len == 1 && data[seg_start] == '%')) {
                cb(data + seg_start, seg_len);
            }
            seg_start = i + 1;
        }
    }
    // Trailing segment (if the packet had no final '#' before '%')
    if (seg_start < i) {
        int seg_len = i - seg_start;
        if (!(seg_len == 1 && data[seg_start] == '%')) {
            cb(data + seg_start, seg_len);
        }
    }
}

// Parse raw bytes into a Packet.
// Returns the number of bytes consumed (0 = no complete packet yet).
// data must be null-terminated or at least data[0..len-1] valid.
inline int parse_packet(const char* data, int len, Packet& out) {
    // Find the terminating '%'
    int end = -1;
    for (int i = 0; i < len; ++i) {
        if (data[i] == '%') { end = i; break; }
    }
    if (end < 0) return 0; // incomplete

    out.field_count = 0;
    std::memset(out.header, 0, sizeof(out.header));

    // Split on '#'
    int seg_start = 0;
    int seg_idx   = -1; // -1 = header, 0+ = fields
    for (int i = 0; i <= end; ++i) {
        char c = (i < end) ? data[i] : '#'; // treat end as delimiter
        if (c == '#' || i == end) {
            int seg_len = i - seg_start;
            if (seg_idx == -1) {
                // Header
                int copy = seg_len < Packet::MAX_HEADER - 1 ? seg_len : Packet::MAX_HEADER - 1;
                std::strncpy(out.header, data + seg_start, copy);
                out.header[copy] = '\0';
            } else if (seg_idx < Packet::MAX_FIELDS) {
                // Check for lone '%' which is the terminator token
                if (seg_len == 1 && data[seg_start] == '%') break;
                int copy = seg_len < Packet::MAX_FIELD_LEN - 1 ? seg_len : Packet::MAX_FIELD_LEN - 1;
                std::strncpy(out.fields[seg_idx], data + seg_start, copy);
                out.fields[seg_idx][copy] = '\0';
                out.field_count = seg_idx + 1;
            }
            seg_start = i + 1;
            ++seg_idx;
        }
    }

    return end + 1; // consumed bytes including the '%'
}

} // namespace ao
