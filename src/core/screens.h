/*
 * File:        src/core/screens.h
 * Description: Streams display files from the filesystem into a session
 *              Timeline in small chunks (never loads a whole file).
 *
 *   Lookup by terminal, first match wins:
 *     PETSCII-40: name.p40, name.seq, name.asc
 *     PETSCII-80: name.p80, name.seq, name.asc
 *     ANSI:       name.ans, name.asc
 *     ASCII:      name.asc
 *
 *   .seq/.p40/.p80 are raw PETSCII, .ans is raw ANSI in CP437 (re-encoded
 *   for UTF-8 callers, SAUCE records skipped), .asc is plain ASCII and is
 *   translated for every terminal.
 *
 *   Paging: .asc files pause every N lines when setPaging(N) is on; the
 *   caller shows a More prompt and calls resume(). Art formats (.ans,
 *   .seq) are drawn with cursor movement and are never paged.
 *
 *   @-codes, case-insensitive, work in all formats:
 *     @BBS@ @VER@ @NODE@ @NODES@ @USER@ @TERM@ @COLS@ @DATE@ @TIME@
 *     @CLS@ @BELL@ @DELAY:ms@ @SPIN:ms@
 * Listing:     COMPLETE FILE
 * Libraries:   none (libc stdio)
 */
#pragma once
#include <cstdint>
#include <cstdio>
#include "../config.h"
#include "term.h"
#include "timeline.h"

class ScreenPlayer {
public:
    struct Vars {
        const char* user;
        uint8_t     node;
        uint8_t     nodes;
    };

    // open: find the best file for this terminal. False if none exists.
    bool open(const char* name, const Term& t);
    bool active() const { return f_ != nullptr; }
    void close();

    // detach: forget the file without closing it (session moved elsewhere)
    void detach() { f_ = nullptr; paused_ = false; }

    // pump: stream as much as fits. Returns true while more remains.
    bool pump(Term& t, Timeline& tl, const Vars& v);

    // paging for .asc files: pause after rows lines (0 = off)
    void setPaging(uint8_t rows) { pageRows_ = rows; }
    bool paused() const { return paused_; }
    void resume()       { paused_ = false; lines_ = 0; }

private:
    enum class Mode : uint8_t { Text, Pet, Ansi };

    void emitByte(Term& t, Timeline& tl, uint8_t b);
    void flushToken(Term& t, Timeline& tl);
    void runToken(Term& t, Timeline& tl, const Vars& v);
    void byteIn(Term& t, Timeline& tl, const Vars& v, uint8_t b);

    FILE*   f_       = nullptr;
    Mode    mode_    = Mode::Text;
    bool    inTok_   = false;
    uint8_t tokLen_  = 0;
    char    tok_[16] = {};    // normalized uppercase ASCII
    uint8_t raw_[16] = {};    // original bytes, for literal fallback
    uint8_t prev_    = 0;
    bool    sauce_   = false; // hit ^Z in an .ans file

    uint8_t buf_[BBS_SCREEN_CHUNK] = {};
    uint8_t bufLen_   = 0;
    uint8_t bufPos_   = 0;
    uint8_t pageRows_ = 0;
    uint8_t lines_    = 0;
    bool    paused_   = false;
};
