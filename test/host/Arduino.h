/**
 * The slice of Arduino this runtime actually uses, on a PC.
 *
 * Measured, not guessed: millis, delay, micros, random, randomSeed, yield,
 * analogRead, Serial.print/println, byte, SERIAL_8N1 and IPAddress. Nothing
 * else is provided, so an accidental new dependency on the core fails to
 * compile here rather than quietly making the host build a different program
 * from the board build.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HOST_ARDUINO_H
#define HOST_ARDUINO_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <thread>

typedef uint8_t byte;

#ifndef SERIAL_8N1
#define SERIAL_8N1 0x06
#endif

// An analog pin number. There is no pin; seedRandom() reads it for entropy.
#ifndef A0
#define A0 0
#endif

// The runtime writes Arduino's unqualified min/max. Arduino's core defines
// them as macros, which here would rewrite std::min inside ArduinoJson and the
// standard library -- so they are functions, which the preprocessor leaves
// alone.
template <typename T> inline T min(T a, T b) { return a < b ? a : b; }
template <typename T> inline T max(T a, T b) { return a > b ? a : b; }

inline unsigned long millis() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return (unsigned long)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}

inline unsigned long micros() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return (unsigned long)duration_cast<microseconds>(steady_clock::now() - t0).count();
}

inline void delay(unsigned long ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

inline void yield() {}

inline void randomSeed(unsigned long seed) { srand((unsigned)seed); }
inline long random(long howbig) { return howbig <= 0 ? 0 : rand() % howbig; }
inline long random(long howsmall, long howbig) {
    return howbig <= howsmall ? howsmall : howsmall + random(howbig - howsmall);
}

/** There is no floating analog pin on a PC; entropy comes from the clock. */
inline int analogRead(int) { return (int)(micros() & 0x3FF); }

class HostSerial {
public:
    void begin(unsigned long) {}
    void print(const char* s) { fputs(s, stdout); }
    void println(const char* s) { fputs(s, stdout); fputc('\n', stdout); }
    void println() { fputc('\n', stdout); }
    explicit operator bool() const { return true; }
};
extern HostSerial Serial;

/** Four octets, comparable, and convertible to what a socket wants. */
class IPAddress {
public:
    IPAddress() { memset(_o, 0, 4); }
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) { _o[0]=a; _o[1]=b; _o[2]=c; _o[3]=d; }
    uint8_t operator[](int i) const { return _o[i & 3]; }
    uint8_t& operator[](int i) { return _o[i & 3]; }
    bool operator==(const IPAddress& o) const { return memcmp(_o, o._o, 4) == 0; }
    bool operator!=(const IPAddress& o) const { return !(*this == o); }
    /** Network byte order, which is also octet order. */
    uint32_t asBigEndian() const {
        return ((uint32_t)_o[0]) | ((uint32_t)_o[1] << 8) |
               ((uint32_t)_o[2] << 16) | ((uint32_t)_o[3] << 24);
    }
private:
    uint8_t _o[4];
};

#endif  // HOST_ARDUINO_H
