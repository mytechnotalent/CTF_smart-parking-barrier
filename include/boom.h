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
// File:    boom.h
// Desc:    Declares the barrier boom state machine that sequences the
//          SG90 actuator and fails open on loss of authority.
// Created: 2026

#ifndef BOOM_H
#define BOOM_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Bounded barrier boom travel time in milliseconds.
 */
#define BOOM_TRAVEL_MS 1000u

/**
 * @brief Barrier boom position and health states.
 */
typedef enum boom_state {
    /**
     * @brief Boom is closed, lowering the boom.
     */
    BOOM_STATE_CLOSED = 0,
    /**
     * @brief Boom is raised, opening the entry lane.
     */
    BOOM_STATE_OPEN = 1,
    /**
     * @brief Boom has failed safe into the open posture.
     */
    BOOM_STATE_FAULT = 2,
    /**
     * @brief Boom actuator is travelling between positions.
     */
    BOOM_STATE_MOVING = 3,
} boom_state_t;

/**
 * @brief Initialize the boom state machine and open the barrier.
 *
 * @param void No parameters.
 * @return void
 */
void boom_init(void);

/**
 * @brief Return the current boom state.
 *
 * @param void No parameters.
 * @return boom_state_t Current boom state.
 */
boom_state_t boom_state(void);

/**
 * @brief Report whether the barrier is currently fully open.
 *
 * @param void No parameters.
 * @return bool true when the barrier is open.
 */
bool boom_is_open(void);

/**
 * @brief Apply an authorized open or close command to the barrier.
 *
 * Unauthorized commands are refused. An authorized command starts a
 * bounded travel interval that boom_tick completes. This is the
 * guarded command path that prevents an unauthenticated local raise from
 * moving the barrier.
 *
 * @param open True to drive the barrier open, false to close it.
 * @param authorized True when the caller has validated the command.
 * @return void
 */
void boom_apply_command(bool open, bool authorized);

/**
 * @brief Advance the boom state machine by one tick.
 *
 * Completes a pending travel once the bounded interval has elapsed.
 *
 * @param void No parameters.
 * @return void
 */
void boom_tick(void);

/**
 * @brief Force the barrier open and record the fault.
 *
 * This is the fail-open posture taken when the control link is lost or a
 * barrier frame cannot be authorized.
 *
 * @param void No parameters.
 * @return void
 */
void boom_fail_safe(void);

#endif // BOOM_H
