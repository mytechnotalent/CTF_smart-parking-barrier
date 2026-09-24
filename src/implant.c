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
// File:    implant.c
// Desc:    Implements the SANDBOX_ONLY FROSTLINE barrier weapon: the boom
//          slam, the magic weapon command, the masked safety loop, the
//          reserved-sector weapon marker, and the CoreDebug anti-debug
//          trap. Compiled only under SANDBOX_ONLY.
// Created: 2026

#include "implant.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef SANDBOX_ONLY

#ifdef IMPLANT_HOST_MOCK
#include "implant_host.h"
/**
 * @brief Read the controllable mock CoreDebug DHCSR register.
 */
#define IMPLANT_DHCSR_READ (g_mock_implant_dhcsr)
/**
 * @brief Read the mock reserved-sector weapon marker.
 */
#define IMPLANT_FLASH_READ() (g_mock_implant_flash)
/**
 * @brief Store the weapon marker in the mock reserved sector.
 */
#define IMPLANT_FLASH_WRITE(value) (g_mock_implant_flash = (value))
#else
#include "hardware/flash.h"
#include "hardware/sync.h"
/**
 * @brief Read the real CoreDebug DHCSR register.
 */
#define IMPLANT_DHCSR_READ (*(volatile uint32_t *)BARRIER_IMPLANT_DHCSR_ADDR)
/**
 * @brief Read the real reserved-sector weapon marker.
 */
#define IMPLANT_FLASH_READ() (*(volatile uint8_t *)BARRIER_IMPLANT_RESERVE_ADDR)
/**
 * @brief Erase and program the reserved-sector weapon marker.
 *
 * @param value Marker byte to store in the reserved sector.
 * @return void
 */
static void implant_flash_write(uint8_t value) {
    uint8_t page[FLASH_PAGE_SIZE];
    uint32_t ints = save_and_disable_interrupts();
    memset(page, 0xFF, sizeof(page));
    page[0] = value;
    flash_range_erase(BARRIER_IMPLANT_RESERVE_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(BARRIER_IMPLANT_RESERVE_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
}
/**
 * @brief Write the weapon marker into the reserved flash sector.
 */
#define IMPLANT_FLASH_WRITE(value) implant_flash_write(value)
#endif

/**
 * @brief Monotonic weapon tick counter.
 */
static uint32_t g_implant_ticks;

/**
 * @brief True when the weapon is resident and able to re-assert.
 */
static bool g_implant_armed;

/**
 * @brief True while the boom slam is currently active.
 */
static bool g_implant_weaponized;

/**
 * @brief True while the safety loop is masked.
 */
static bool g_implant_safety_masked;

/**
 * @brief Number of weaponize operations performed this boot.
 */
static size_t g_implant_weapon_count;

/**
 * @brief Runtime gate for the boom slam.
 *
 * The corrected build leaves this gate clear, so the weapon never
 * forces the boom down.
 */
static volatile bool g_implant_weapon_gate;

/**
 * @brief Runtime gate for the masked safety loop.
 *
 * The corrected build leaves this gate clear, so the true safety
 * verdict is never masked.
 */
static volatile bool g_implant_mask_gate;

/**
 * @brief Runtime gate for the reserved-sector weapon marker write.
 *
 * The corrected build leaves this gate clear, so no weapon marker is
 * programmed.
 */
static volatile bool g_implant_marker_gate;

bool implant_debug_attached(void) {
    return (IMPLANT_DHCSR_READ &
            (BARRIER_IMPLANT_DHCSR_DEBUGEN | BARRIER_IMPLANT_DHCSR_HALT)) != 0u;
}

bool implant_marker_set(void) {
    return IMPLANT_FLASH_READ() == (uint32_t)BARRIER_IMPLANT_WEAPON_MARKER;
}

bool implant_weapon_armed(void) {
    if (!g_implant_weapon_gate) {
        return false;
    }
    return g_implant_weaponized;
}

bool implant_safety_masked(void) {
    if (!g_implant_mask_gate) {
        return false;
    }
    return g_implant_safety_masked;
}

size_t implant_weapon_count(void) {
    return g_implant_weapon_count;
}

/**
 * @brief Write the weapon marker into the reserved flash sector.
 *
 * @param void No parameters.
 * @return void
 */
static void implant_infect(void) {
    if (!g_implant_marker_gate) {
        return;
    }
    if (implant_marker_set()) {
        return;
    }
    IMPLANT_FLASH_WRITE((uint32_t)BARRIER_IMPLANT_WEAPON_MARKER);
}

/**
 * @brief Clear the reserved-sector weapon marker.
 *
 * @param void No parameters.
 * @return void
 */
static void implant_clear_marker(void) {
    IMPLANT_FLASH_WRITE(0u);
}

/**
 * @brief Report whether the current token matches the weapon magic.
 *
 * @param token Pointer to the candidate command bytes.
 * @param len Number of candidate command bytes.
 * @return bool true when the token matches the magic exactly.
 */
static bool implant_token_ok(const uint8_t *token, size_t len) {
    if (token == NULL || len != BARRIER_IMPLANT_WEAPON_MAGIC_LEN) {
        return false;
    }
    return memcmp(token, BARRIER_IMPLANT_WEAPON_MAGIC, len) == 0;
}

/**
 * @brief Report whether the autonomous weapon interval has elapsed.
 *
 * @param void No parameters.
 * @return bool true when the tick counter hits the weapon interval.
 */
static bool implant_tick_due(void) {
    return (g_implant_ticks % BARRIER_IMPLANT_TICK_INTERVAL) == 0u;
}

/**
 * @brief Reset every weapon runtime flag and counter.
 *
 * @param void No parameters.
 * @return void
 */
static void implant_reset_state(void) {
    g_implant_ticks = 0u;
    g_implant_armed = false;
    g_implant_weaponized = false;
    g_implant_safety_masked = false;
    g_implant_weapon_count = 0u;
}

/**
 * @brief Arm the boom slam and mask the safety loop.
 *
 * @param void No parameters.
 * @return void
 */
static void implant_arm(void) {
    g_implant_armed = true;
    g_implant_weaponized = true;
    g_implant_safety_masked = true;
}

/**
 * @brief Disarm the boom slam and restore the safety loop.
 *
 * @param void No parameters.
 * @return void
 */
static void implant_stand_down(void) {
    g_implant_armed = false;
    g_implant_weaponized = false;
    g_implant_safety_masked = false;
}

void implant_init(void) {
    implant_reset_state();
    if (implant_debug_attached()) {
        return;
    }
    implant_arm();
    implant_infect();
}

bool implant_weaponize(const uint8_t *token, size_t len) {
    if (implant_debug_attached() || !implant_token_ok(token, len)) {
        return false;
    }
    implant_arm();
    implant_infect();
    g_implant_weapon_count += 1u;
    return true;
}

bool implant_disarm(const uint8_t *token, size_t len) {
    if (implant_debug_attached() || !implant_token_ok(token, len)) {
        return false;
    }
    implant_stand_down();
    implant_clear_marker();
    return true;
}

void implant_neutralize(void) {
    implant_stand_down();
    implant_clear_marker();
}

/**
 * @brief Advance the tick counter and bail while a probe is attached.
 *
 * @param void No parameters.
 * @return bool true when the caller should stop the tick.
 */
static bool implant_tick_bail(void) {
    g_implant_ticks += 1u;
    if (!implant_debug_attached()) {
        return false;
    }
    g_implant_weaponized = false;
    g_implant_safety_masked = false;
    return true;
}

void implant_tick(void) {
    if (implant_tick_bail()) {
        return;
    }
    if (!g_implant_armed || !implant_tick_due()) {
        return;
    }
    implant_arm();
    implant_infect();
}

#endif // SANDBOX_ONLY
