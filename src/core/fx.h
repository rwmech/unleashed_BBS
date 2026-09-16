/*
 * File:        src/core/fx.h
 * Description: Old-school TTY effects, each a reusable routine that
 *              queues timed frames into a session Timeline. Nothing here
 *              blocks; the scheduler plays the frames back. Every effect
 *              is written against Term primitives, so it renders on
 *              PETSCII, ANSI and ASCII without per-effect special cases.
 *
 *              Rules for effect authors:
 *              - Stay on one line. PETSCII has no bare carriage return,
 *                so rewinds use cursor-left or DEL, never '\r'.
 *              - Size steps with fitSteps() so an effect never overflows
 *                the Timeline. If space is short the effect degrades to
 *                fewer steps, never to garbage.
 * Listing:     COMPLETE FILE
 * Libraries:   none
 */
#pragma once
#include <cstdint>
#include "term.h"
#include "timeline.h"

namespace fx {

enum class Spin : uint8_t {
    Line,    // | / - \   (quadrant rotation on PETSCII)
    Dots,    // . o O o
    Arrow,   // ^ > v <
};

// fitSteps: how many animation steps fit in the Timeline right now
uint16_t fitSteps(const Timeline& tl, uint16_t wanted, uint16_t bytesPerStep);

// --- timing ---------------------------------------------------------------
void pause(Timeline& tl, uint16_t ms);
void baud(Timeline& tl, uint32_t bps);        // emulate line speed, 0 = off

// --- text reveal -----------------------------------------------------------
void typewriter(Term& t, Timeline& tl, const char* s, uint16_t msPerChar);
void scramble(Term& t, Timeline& tl, const char* s, uint8_t rounds, uint16_t ms);
void blink(Term& t, Timeline& tl, const char* s, uint8_t times, uint16_t ms);
void marquee(Term& t, Timeline& tl, const char* s, uint8_t width, uint16_t ms, uint8_t loops);

// --- erase / rewrite -------------------------------------------------------
void rubout(Term& t, Timeline& tl, uint8_t n, uint16_t msEach);
void typeRubout(Term& t, Timeline& tl, const char* s,
                uint16_t msType, uint16_t hold, uint16_t msErase);
void rewrite(Term& t, Timeline& tl, uint8_t oldLen, const char* newText);

// --- activity indicators ---------------------------------------------------
void dots(Term& t, Timeline& tl, uint8_t count, uint16_t msEach);
void spinner(Term& t, Timeline& tl, Spin style, uint16_t totalMs, uint16_t msEach);
void cursorBlink(Term& t, Timeline& tl, uint8_t times, uint16_t ms);
void working(Term& t, Timeline& tl, const char* label, uint16_t ms, const char* result);
void progressBar(Term& t, Timeline& tl, uint8_t width, uint16_t totalMs);
void countdown(Term& t, Timeline& tl, uint8_t from, uint16_t msEach, const char* finalText);

// --- line noise / modem --------------------------------------------------
void lineNoise(Term& t, Timeline& tl, uint8_t n, uint16_t hold);
void hangup(Term& t, Timeline& tl);
void bell(Term& t, Timeline& tl);

// --- decoration ------------------------------------------------------------
void rule(Term& t, Timeline& tl, uint8_t width);

} // namespace fx
