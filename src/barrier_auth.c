// MIT License
//
// Copyright (c) 2026 Kevin Thomas
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// Author:  Kevin Thomas
// Email:   kevin@mytechnotalent.com
// GitHub:  https://github.com/mytechnotalent/smart-parking-barrier
// File:    barrier_auth.c
// Desc:    Implements the barrier command anti-replay window and the keyed
//          state tag that guards the accepted barrier verdict.
// Created: 2026

#include "barrier_auth.h"
#include "crypto_aead.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**
 * @brief Field key used to compute and verify state tags.
 */
static uint8_t g_auth_key[CRYPTO_AEAD_KEY_LEN];

/**
 * @brief True once a field key has been installed.
 */
static bool g_auth_key_ready;

/**
 * @brief Write one 32-bit value in little-endian order.
 *
 * @param out Pointer to the four-byte output.
 * @param value Value to serialize.
 * @return void
 */
static void barrier_auth_put_u32(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8u) & 0xFFu);
    out[2] = (uint8_t)((value >> 16u) & 0xFFu);
    out[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

/**
 * @brief Serialize the authenticated fields of a record.
 *
 * @param auth Pointer to the authorization record.
 * @param rec Pointer to the BARRIER_AUTH_RECORD_LEN output buffer.
 * @return void
 */
static void barrier_auth_serialize(const barrier_auth_t *auth,
                                 uint8_t rec[BARRIER_AUTH_RECORD_LEN]) {
    rec[0] = auth->granted ? 1u : 0u;
    barrier_auth_put_u32(&rec[1], auth->seq);
    barrier_auth_put_u32(&rec[5], auth->last_seq);
}

/**
 * @brief Build the deterministic state-tag nonce for a sequence number.
 *
 * @param seq Sequence number bound into the nonce.
 * @param nonce Pointer to the 24-byte nonce output buffer.
 * @return void
 */
static void barrier_auth_nonce(uint32_t seq,
                             uint8_t nonce[CRYPTO_AEAD_NONCE_LEN]) {
    memset(nonce, 0, CRYPTO_AEAD_NONCE_LEN);
    barrier_auth_put_u32(nonce, seq);
    nonce[4] = BARRIER_AUTH_NONCE_DOMAIN;
}

void barrier_auth_init(barrier_auth_t *auth) {
    if (auth == NULL) {
        return;
    }
    memset(auth, 0, sizeof(*auth));
}

void barrier_auth_set_key(const uint8_t key[CRYPTO_AEAD_KEY_LEN]) {
    if (key == NULL) {
        g_auth_key_ready = false;
        return;
    }
    memcpy(g_auth_key, key, CRYPTO_AEAD_KEY_LEN);
    g_auth_key_ready = true;
}

bool barrier_auth_state_tag(const barrier_auth_t *auth,
                          uint8_t tag[CRYPTO_AEAD_TAG_LEN]) {
    uint8_t record[BARRIER_AUTH_RECORD_LEN];
    uint8_t nonce[CRYPTO_AEAD_NONCE_LEN];
    uint8_t ct[BARRIER_AUTH_RECORD_LEN];
    if (!g_auth_key_ready || auth == NULL || tag == NULL) return false;
    barrier_auth_serialize(auth, record);
    barrier_auth_nonce(auth->seq, nonce);
    return crypto_aead_seal(g_auth_key, nonce, record, sizeof(record),
                            record, 0u, ct, tag);
}

bool barrier_auth_state_ok(const barrier_auth_t *auth) {
    uint8_t expect[CRYPTO_AEAD_TAG_LEN];
    if (auth == NULL || !barrier_auth_state_tag(auth, expect)) {
        return false;
    }
    return crypto_aead_tag_equal(expect, auth->tag);
}

/**
 * @brief Build the record that an accepted command would produce.
 *
 * @param seq Sequence number carried by the command.
 * @param candidate Pointer to the candidate authorization record.
 * @return void
 */
static void barrier_auth_candidate(uint32_t seq, barrier_auth_t *candidate) {
    barrier_auth_init(candidate);
    candidate->granted = true;
    candidate->seq = seq;
    candidate->last_seq = seq;
}

/**
 * @brief Report whether a command tag matches the record it would produce.
 *
 * @param seq Sequence number carried by the command.
 * @param tag Pointer to the 16-byte command tag to verify.
 * @return bool true when the tag matches the candidate record.
 */
static bool barrier_auth_tag_matches(uint32_t seq,
                                   const uint8_t tag[CRYPTO_AEAD_TAG_LEN]) {
    barrier_auth_t candidate;
    uint8_t expect[CRYPTO_AEAD_TAG_LEN];
    barrier_auth_candidate(seq, &candidate);
    if (!barrier_auth_state_tag(&candidate, expect)) {
        return false;
    }
    return crypto_aead_tag_equal(expect, tag);
}

/**
 * @brief Write an accepted command into the authorization record.
 *
 * @param auth Pointer to the authorization record.
 * @param seq Sequence number carried by the command.
 * @param tag Pointer to the verified 16-byte command tag.
 * @return void
 */
static void barrier_auth_accept(barrier_auth_t *auth, uint32_t seq,
                              const uint8_t tag[CRYPTO_AEAD_TAG_LEN]) {
    auth->granted = true;
    auth->seq = seq;
    auth->last_seq = seq;
    memcpy(auth->tag, tag, CRYPTO_AEAD_TAG_LEN);
}

bool barrier_auth_apply(barrier_auth_t *auth, uint32_t seq,
                      const uint8_t tag[CRYPTO_AEAD_TAG_LEN]) {
    if (auth == NULL || tag == NULL || seq <= auth->last_seq) {
        return false;
    }
    if (!barrier_auth_tag_matches(seq, tag)) {
        return false;
    }
    barrier_auth_accept(auth, seq, tag);
    return true;
}
