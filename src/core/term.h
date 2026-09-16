/*
 * File:        src/core/term.h
 * Description: Terminal layer. One interface, four drivers: PETSCII-40,
 *              PETSCII-80, ANSI (CP437 or UTF-8) and plain ASCII.
 *              Everything above this layer writes ASCII strings plus
 *              abstract operations (color, cursor, glyph). Plugins must
 *              never emit raw control codes.
 * Listing:     COMPLETE FILE
 * Libraries:   none
 */
#pragma once
#include <cstdint>
#include <cstddef>

// ---------------------------------------------------------------------------
// ByteSink: anything that accepts outgoing bytes (Timeline, raw socket)
// ---------------------------------------------------------------------------
class ByteSink {
public:
    virtual ~ByteSink() = default;
    virtual bool put(const uint8_t* data, size_t len) = 0;
    bool putc(uint8_t b) { return put(&b, 1); }
    bool puts(const char* s);
};

enum class TermType : uint8_t { Unknown, Ascii, Ansi, Pet40, Pet80 };
enum class Charset  : uint8_t { Ascii, Cp437, Utf8, Petscii };

// C64 palette order, mapped to the nearest ANSI color on PC terminals
enum class Color : uint8_t {
    Black, White, Red, Cyan, Purple, Green, Blue, Yellow,
    Orange, Brown, LightRed, DarkGrey, Grey, LightGreen, LightBlue, LightGrey
};

// Drawing glyphs with a per-terminal best match
enum class Glyph : uint8_t { Block, Shade, HLine, VLine, Bullet };

// Canonical key codes returned by Term::feed (printables are plain ASCII)
enum Key : int {
    KEY_NONE      = -1,
    KEY_ENTER     = 0x100,
    KEY_BACKSPACE,
    KEY_ESC,        // ESC on PC, left-arrow key on C64 (classic abort key)
    KEY_BREAK,      // Ctrl-C on PC, RUN/STOP on C64
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_HOME,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8,
};

using KeyFn = void (*)(void* ctx, int key);

class Term {
public:
    // -- setup ---------------------------------------------------------
    void setType(TermType t, Charset cs, uint8_t cols, uint8_t rows);
    void setGeometry(uint8_t cols, uint8_t rows) { if (cols) cols_ = cols; if (rows) rows_ = rows; }
    // telnet sessions must double any 0xFF data byte (IAC IAC)
    void setIacEscape(bool on) { iacEsc_ = on; }
    bool iacEscape()   const { return iacEsc_; }
    TermType type()    const { return type_; }
    Charset  charset() const { return cs_; }
    uint8_t  cols()    const { return cols_; }
    uint8_t  rows()    const { return rows_; }
    bool     isPet()   const { return type_ == TermType::Pet40 || type_ == TermType::Pet80; }
    bool     isAnsi()  const { return type_ == TermType::Ansi; }
    const char* name() const { return nameOf(type_, cs_); }
    static const char* nameOf(TermType t, Charset cs);

    // init: put the remote terminal in a known state (charset, colors, cls)
    void init(ByteSink& o);

    // -- output ----------------------------------------------------------
    void ch(ByteSink& o, char c);                  // one ASCII char, translated
    void text(ByteSink& o, const char* s);         // ASCII string, '\n' = newline
    void textN(ByteSink& o, const char* s, size_t n);
    void nl(ByteSink& o);
    void cls(ByteSink& o);
    void color(ByteSink& o, Color c);
    void reset(ByteSink& o);                       // default color, no reverse
    void reverse(ByteSink& o, bool on);
    void left(ByteSink& o, uint8_t n);             // non-destructive moves
    void right(ByteSink& o, uint8_t n);
    void up(ByteSink& o, uint8_t n);
    void down(ByteSink& o, uint8_t n);
    void home(ByteSink& o);
    void gotoXY(ByteSink& o, uint8_t x, uint8_t y); // 1-based
    void eraseBack(ByteSink& o, uint8_t n);        // destructive backspace x n
    void eraseEol(ByteSink& o, uint8_t hint);      // ANSI ESC[K, else hint spaces
    void glyph(ByteSink& o, Glyph g);
    void glyphs(ByteSink& o, Glyph g, uint8_t n);
    void bell(ByteSink& o);
    void cursor(ByteSink& o, bool visible);        // ANSI only
    void cp437(ByteSink& o, uint8_t b);            // one CP437 byte, re-encoded

    // -- input -----------------------------------------------------------
    // feed: raw byte (after telnet filtering) -> zero or more key events
    void feed(uint8_t b, KeyFn fn, void* ctx);

    // idle: call when input has been quiet; resolves a lone ESC keypress
    void idle(KeyFn fn, void* ctx);

private:
    TermType type_ = TermType::Unknown;
    Charset  cs_   = Charset::Ascii;
    uint8_t  cols_ = 80;
    uint8_t  rows_ = 24;
    bool     rev_  = false;
    bool     iacEsc_ = false;

    // input escape parser (ANSI arrow keys, stray CPR replies)
    uint8_t  esc_      = 0;   // 0 idle, 1 got ESC, 2 in CSI, 3 in SS3
    uint8_t  escParam_ = 0;   // first numeric CSI parameter
};
