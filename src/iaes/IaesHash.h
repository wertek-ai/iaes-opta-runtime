/**
 * iaes-opta-runtime — SHA-256
 *
 * The specification defines content_hash as the first 16 characters of the
 * SHA-256 hex digest of the canonical data payload. This is that, with no
 * dependency and no heap allocation, so a reader has nothing to install and a
 * microcontroller has nothing to fragment.
 *
 * It replaces an FNV-1a digest that was fast and wrong: the same reading
 * hashed differently here than in the Python and TypeScript SDKs, so a
 * consumer deduplicating across sources silently kept both copies.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IAES_HASH_H
#define IAES_HASH_H

#include <stddef.h>
#include <stdint.h>

class IaesHash {
public:
    /** Streaming SHA-256. Feed with update(), finish with digest(). */
    IaesHash();
    void update(const uint8_t* data, size_t len);
    void update(const char* text);
    /** Writes 32 raw bytes. The object must not be reused afterwards. */
    void digest(uint8_t out[32]);

    /**
     * The specification's content_hash: the first 16 characters of the
     * lowercase hex digest. Needs a 17-byte buffer.
     */
    static void contentHash(const char* canonical_json, char* out, size_t len);

private:
    void compress(const uint8_t block[64]);
    uint32_t state_[8];
    uint64_t bits_;
    uint8_t  buf_[64];
    size_t   buf_len_;
};

#endif // IAES_HASH_H
