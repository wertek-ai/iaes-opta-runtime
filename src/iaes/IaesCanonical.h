/**
 * iaes — Canonical form of a data payload
 *
 * The specification hashes "canonical JSON, sorted keys". Two producers that
 * disagree on what canonical means produce different content_hash for the same
 * reading, and a consumer deduplicating across them keeps both copies without
 * noticing -- which is what this runtime did until 2026-09-06.
 *
 * Deliberately free of Arduino, so the conformance test can run on a host and
 * be compared against the Python and TypeScript SDKs.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IAES_CANONICAL_H
#define IAES_CANONICAL_H

#include <ArduinoJson.h>
#include <stddef.h>

namespace iaes {

/**
 * Compact JSON with keys sorted, and whole numbers written without a decimal
 * point so that 25600.0 here matches 25600 in JavaScript and in Python, whose
 * SDK normalizes the same way.
 *
 * Returns the number of characters written, or 0 if the buffer was too small.
 * Never truncates: a truncated canonical form hashes two different payloads to
 * the same digest, which is worse than refusing to answer.
 */
size_t canonicalize(JsonVariantConst value, char* out, size_t len);

}  // namespace iaes

#endif // IAES_CANONICAL_H
