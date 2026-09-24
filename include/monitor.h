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
// File:    monitor.h
// Desc:    Declares the IRON FANG smart parking barrier state machine
//          that ties the monthly-pass remote, the sealed pass/raise
//          command path, the cabinet temperature sensor, the boom
//          actuator, and the parking control gateway link together.
// Created: 2026

#ifndef MONITOR_H
#define MONITOR_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Onboard heartbeat LED pulse width in microseconds.
 */
#define MONITOR_HEARTBEAT_US 2000u

/**
 * @brief Initialize the smart parking barrier state machine.
 *
 * Configures the I2C LCD, the DHT11 cabinet temperature sensor, the
 * infrared monthly-pass remote, the tower light lamps, the boom servo,
 * the manual raise button, the RYLR998 radio, and derives the Argon2id
 * field key.
 *
 * @param void No parameters.
 * @return bool true when all submodules initialized.
 */
bool monitor_init(void);

/**
 * @brief Clear the node-ready flag and command path.
 *
 * @param void No parameters.
 * @return void
 */
void monitor_deinit(void);

/**
 * @brief Clear a pending manual pass request.
 *
 * @param void No parameters.
 * @return void
 */
void monitor_pass_request(void);

/**
 * @brief Execute one smart parking barrier tick.
 *
 * Polls the monthly-pass remote and the radio, verifies and applies
 * sealed pass and raise commands, honors the safety interlock, drives
 * the boom and tower light, renders the barrier status, and fails safe
 * to the raised posture on a lost control link. A manual raise never
 * bypasses authorization and untrusted frames are never applied.
 *
 * @param void No parameters.
 * @return bool true when the tick completed without a policy error.
 */
bool monitor_step(void);

#endif // MONITOR_H
