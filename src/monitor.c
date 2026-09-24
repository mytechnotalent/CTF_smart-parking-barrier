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
// File:    monitor.c
// Desc:    Implements the IRON FANG smart parking barrier state machine
//          that ties the monthly-pass remote, the sealed pass/raise
//          command path, the cabinet temperature sensor, the boom
//          actuator, and the RYLR998 parking control link together. The
//          production build never applies an untrusted frame, never
//          lets a manual raise bypass authorization, honors the safety
//          interlock, and fails safe: the SANDBOX_ONLY implant is the
//          only covert path and it is compiled out of the clean build.
// Created: 2026

#include "barrier.h"
#include "monitor.h"
#include "sensor.h"
#include "display.h"
#include "radio.h"
#include "status_led.h"
#include "button.h"
#include "servo.h"
#include "ir_remote.h"
#include "boom.h"
#include "control.h"
#include "implant.h"
#include "crypto_aead.h"
#include "crypto_kdf.h"
#include "field_secrets.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/uart.h"
#include "pico/time.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief Guarded barrier states derived from authorized barrier commands.
 */
typedef enum barrier_state {
    /**
     * @brief The boom is raised and the lane is clear.
     */
    BARRIER_STATE_OPEN = 0,
    /**
     * @brief A monthly pass is pending authorization.
     */
    BARRIER_STATE_PASS_PENDING = 1,
    /**
     * @brief The lane is denied and the boom is lowered.
     */
    BARRIER_STATE_DENIED = 2,
} barrier_state_t;

/**
 * @brief Module-ready flag.
 */
static bool g_ready;

/**
 * @brief Initialized I2C peripheral handle for the LCD backpack.
 */
static i2c_inst_t *g_i2c;

/**
 * @brief Initialized I2C backpack address for the LCD.
 */
static uint8_t g_i2c_addr;

/**
 * @brief Derived XChaCha20-Poly1305 field key for the control link.
 */
static uint8_t g_key[CRYPTO_AEAD_KEY_LEN];

/**
 * @brief True once the field key has been derived and installed.
 */
static bool g_key_ready;

/**
 * @brief True once a sealed barrier command has been accepted.
 */
static bool g_link_seen;

/**
 * @brief Absolute time in microseconds of the last accepted command.
 */
static uint64_t g_last_rx_us;

/**
 * @brief Last observed cabinet temperature in-range verdict.
 */
static bool g_temp_ok;

/**
 * @brief Last observed safety interlock verdict from the cabinet sensor.
 */
static bool g_safety_ok;

/**
 * @brief Last observed cabinet temperature in tenths of a degree Celsius.
 */
static int16_t g_temp_tenths;

/**
 * @brief Guarded barrier state.
 */
static barrier_state_t g_state;

/**
 * @brief Parking zone recovered from the last accepted command.
 */
static int16_t g_zone;

/**
 * @brief True while a manual pass awaits authorization.
 */
static bool g_pass_pending;

/**
 * @brief Last weapon state applied to the boom and display.
 */
static bool g_weapon_shown;

/**
 * @brief First LCD barrier render line buffer.
 */
static char g_line1[DISPLAY_LINE_LEN];

/**
 * @brief Second LCD barrier render line buffer.
 */
static char g_line2[DISPLAY_LINE_LEN];

/**
 * @brief Inbound radio line accumulator.
 */
static char g_rx_line[RADIO_LINE_BUF_LEN];

/**
 * @brief Number of bytes currently held in the inbound line accumulator.
 */
static size_t g_rx_len;

/**
 * @brief Monotonic reading-cycle counter for the interactive console.
 */
static uint32_t g_cycles;

/**
 * @brief Probe one I2C address and report whether it acknowledges.
 *
 * @param i2c Pointer to the I2C peripheral to probe.
 * @param addr The 7-bit address to probe.
 * @return bool true when the address acknowledged.
 */
static bool i2c_probe(i2c_inst_t *i2c, uint8_t addr) {
    uint8_t dummy = 0u;
    if (i2c_write_blocking(i2c, addr, &dummy, 1u, false) < 0) {
        return false;
    }
    printf("  found 0x%02X\n", (unsigned)addr);
    return true;
}

/**
 * @brief Probe the I2C bus and print every device that acknowledges.
 *
 * @param i2c Pointer to the I2C peripheral to scan.
 * @return void
 */
static void i2c_bus_scan(i2c_inst_t *i2c) {
    uint8_t addr;
    uint8_t found = 0u;
    printf("I2C scan:\n");
    for (addr = 0x08u; addr < 0x78u; ++addr) {
        found += i2c_probe(i2c, addr) ? 1u : 0u;
    }
    if (found == 0u) {
        printf("  no devices\n");
    }
}

/**
 * @brief Initialize the I2C bus pins and scan the bus.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_bus_init(void) {
    i2c_init(BARRIER_I2C, BARRIER_I2C_BAUD);
    gpio_set_function(BARRIER_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(BARRIER_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(BARRIER_I2C_SDA);
    gpio_pull_up(BARRIER_I2C_SCL);
    i2c_bus_scan(BARRIER_I2C);
}

/**
 * @brief Configure the onboard heartbeat LED.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_gpio_init(void) {
    gpio_init(BARRIER_LED_PIN);
    gpio_set_dir(BARRIER_LED_PIN, GPIO_OUT);
    gpio_put(BARRIER_LED_PIN, 0);
}

/**
 * @brief Pulse the onboard heartbeat LED once.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_heartbeat(void) {
    gpio_put(BARRIER_LED_PIN, 1);
    sleep_us(MONITOR_HEARTBEAT_US);
    gpio_put(BARRIER_LED_PIN, 0);
}

/**
 * @brief Clear every latched barrier command and interlock flag.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_reset_link(void) {
    g_link_seen = false;
    g_last_rx_us = 0u;
    g_temp_ok = false;
    g_safety_ok = true;
    g_temp_tenths = 0;
    g_state = BARRIER_STATE_OPEN;
    g_zone = 0;
    g_pass_pending = false;
}

/**
 * @brief Clear every latched barrier state flag.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_reset_state(void) {
    monitor_reset_link();
    g_weapon_shown = false;
}

/**
 * @brief Report whether the safety interlock permits lowering the boom.
 *
 * The interlock is the cabinet temperature verdict unless a SANDBOX_ONLY
 * implant has masked the safety loop, in which case the loop falsely
 * reports clear and the barrier never yields.
 *
 * @param void No parameters.
 * @return bool true when the safety loop is clear.
 */
static bool monitor_safety_clear(void) {
#ifdef SANDBOX_ONLY
    if (implant_safety_masked()) {
        return true;
    }
#endif
    return g_safety_ok;
}

/**
 * @brief Report whether the SANDBOX_ONLY implant is forcing a boom slam.
 *
 * @param void No parameters.
 * @return bool true when the implant holds the boom down.
 */
static bool monitor_weapon_slam(void) {
#ifdef SANDBOX_ONLY
    return implant_weapon_armed();
#else
    return false;
#endif
}

/**
 * @brief Compute the boom target, honoring the safety interlock.
 *
 * @param void No parameters.
 * @return bool true when the boom should be raised.
 */
static bool monitor_raise_target(void) {
    if (g_state != BARRIER_STATE_DENIED) {
        return true;
    }
    return !monitor_safety_clear();
}

/**
 * @brief Drive the barrier boom for the current guarded barrier state.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_apply_state(void) {
    bool raise = monitor_raise_target();
    if (monitor_weapon_slam()) {
        raise = false;
    }
    boom_apply_command(raise, true);
}

/**
 * @brief Initialize the LED, LCD handles, boom, and barrier state.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_state_init(void) {
    monitor_gpio_init();
    g_i2c = BARRIER_I2C;
    g_i2c_addr = BARRIER_LCD_ADDR;
    monitor_reset_state();
    boom_init();
    monitor_apply_state();
    g_ready = true;
}

/**
 * @brief Initialize the human interface and actuator peripherals.
 *
 * @param void No parameters.
 * @return bool true when the LEDs, button, servo, and infrared eye ready.
 */
static bool monitor_peripherals_init(void) {
    return status_led_init() && raise_init() && servo_init() &&
           ir_remote_init();
}

/**
 * @brief Initialize the SANDBOX_ONLY implant when it is compiled in.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_implant_init(void) {
#ifdef SANDBOX_ONLY
    implant_init();
#endif
}

/**
 * @brief Re-apply the boom when the SANDBOX_ONLY weapon state changes.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_sync_weapon(void) {
#ifdef SANDBOX_ONLY
    bool armed = implant_weapon_armed();
    if (armed == g_weapon_shown) {
        return;
    }
    g_weapon_shown = armed;
    monitor_apply_state();
#endif
}

/**
 * @brief Derive the field key from the committed lab secret.
 *
 * LAB-ONLY: production must provision the field key through OTP rather
 * than deriving it from a committed passphrase and salt.
 *
 * @param void No parameters.
 * @return bool true when the field key was derived and installed.
 */
static bool monitor_derive_key(void) {
    bool ok = crypto_kdf_argon2id((const uint8_t *)FIELD_SECRET_PASSPHRASE,
                                  strlen(FIELD_SECRET_PASSPHRASE),
                                  FIELD_SECRET_SALT, 16u, g_key);
    g_key_ready = ok;
    control_set_key(ok ? g_key : NULL);
    return ok;
}

/**
 * @brief Print the boot banner and the interactive console control hint.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_banner(void) {
    printf("=== OPERATION IRON FANG // ACT IX SMART PARKING BARRIER ===\n");
    printf("Remote: CH+ 0x47 PASS, CH- 0x45 TEST, CH 0x46 ACK\n");
    printf("Button: manual raise request, never bypasses authorization\n");
}

/**
 * @brief Bring up the barrier state and command path.
 *
 * @param void No parameters.
 * @return bool true when the field key was installed.
 */
static bool monitor_start(void) {
    monitor_state_init();
    control_init();
    monitor_implant_init();
    monitor_sync_weapon();
    monitor_banner();
    return monitor_derive_key();
}

bool monitor_init(void) {
    monitor_bus_init();
    if (!monitor_peripherals_init() || !sensor_init() ||
        !radio_init(BARRIER_UART) ||
        !display_init(BARRIER_I2C, BARRIER_LCD_ADDR)) {
        printf("INIT FAIL\n");
        return false;
    }
    return monitor_start();
}

void monitor_deinit(void) {
    g_ready = false;
    control_deinit();
}

void monitor_pass_request(void) {
    g_pass_pending = false;
}

/**
 * @brief Map an authorized command byte to the guarded barrier state.
 *
 * @param command Guarded barrier command code.
 * @return barrier_state_t Guarded barrier state for the command.
 */
static barrier_state_t monitor_state_for(uint8_t command) {
    if (command == BARRIER_COMMAND_RAISE) return BARRIER_STATE_OPEN;
    if (command == BARRIER_COMMAND_LOWER) return BARRIER_STATE_DENIED;
    if (command == BARRIER_COMMAND_PASS) return BARRIER_STATE_PASS_PENDING;
    return BARRIER_STATE_DENIED;
}

/**
 * @brief Map a guarded barrier state to its tower light lamp.
 *
 * @param state Guarded barrier state to map.
 * @return barrier_led_state_t Tower light state for the barrier state.
 */
static barrier_led_state_t monitor_led_for(barrier_state_t state) {
#ifdef SANDBOX_ONLY
    if (implant_weapon_armed()) return BARRIER_PASS_PENDING;
#endif
    if (state == BARRIER_STATE_DENIED) return BARRIER_DENIED;
    if (g_pass_pending || state == BARRIER_STATE_PASS_PENDING) {
        return BARRIER_PASS_PENDING;
    }
    return BARRIER_OPEN;
}

/**
 * @brief Render a guarded barrier state as a short status label.
 *
 * @param state Guarded barrier state to render.
 * @return const char* NUL-terminated state label.
 */
static const char *monitor_state_text(barrier_state_t state) {
#ifdef SANDBOX_ONLY
    if (implant_weapon_armed()) return "SLAM";
#endif
    if (state == BARRIER_STATE_OPEN) return "OPEN";
    if (state == BARRIER_STATE_PASS_PENDING) return "PASS";
    if (state == BARRIER_STATE_DENIED) return "DENY";
    return "FAIL";
}

/**
 * @brief Render the control link status as a short label.
 *
 * @param void No parameters.
 * @return const char* NUL-terminated link label.
 */
static const char *monitor_link_text(void) {
    return g_link_seen ? "UP" : "--";
}

/**
 * @brief Render the implant weapon marker as a short label.
 *
 * @param void No parameters.
 * @return const char* NUL-terminated weapon label.
 */
static const char *monitor_weapon_text(void) {
#ifdef SANDBOX_ONLY
    return implant_marker_set() ? "WPN" : "--";
#else
    return "--";
#endif
}

/**
 * @brief Format the active parking zone as a short decimal text.
 *
 * @param out Pointer to the mutable text buffer.
 * @param out_len Capacity of the text buffer in bytes.
 * @return void
 */
static void monitor_zone_text(char *out, size_t out_len) {
    snprintf(out, out_len, "%d", (int)g_zone);
}

/**
 * @brief Format the cabinet temperature as a short decimal text.
 *
 * @param out Pointer to the mutable text buffer.
 * @param out_len Capacity of the text buffer in bytes.
 * @return void
 */
static void monitor_temp_text(char *out, size_t out_len) {
    snprintf(out, out_len, "%d", (int)g_temp_tenths);
}

/**
 * @brief Format the barrier status and zone lines into the render buffers.
 *
 * @param zone Pointer to the formatted zone text.
 * @param temp Pointer to the formatted temperature text.
 * @return void
 */
static void monitor_format_lines(const char *zone, const char *temp) {
    snprintf(g_line1, DISPLAY_LINE_LEN, "ST:%-5s L:%s",
             monitor_state_text(g_state), monitor_link_text());
    snprintf(g_line2, DISPLAY_LINE_LEN, "Z:%s T:%s K:%s",
             zone, temp, monitor_weapon_text());
}

/**
 * @brief Render the barrier status and zone lines to the 1602 LCD.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_render(void) {
    char zone[8];
    char temp[8];
    monitor_zone_text(zone, sizeof(zone));
    monitor_temp_text(temp, sizeof(temp));
    monitor_format_lines(zone, temp);
    display_render_lines(g_i2c, g_i2c_addr, g_line1, g_line2);
}

/**
 * @brief Sample the DHT11 cabinet sensor and classify it.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_refresh_temp(void) {
    dht_reading_t reading;
    g_cycles += 1u;
    if (sensor_read(&reading) != SENSOR_RESULT_OK) {
        printf("CAB read failed -> WARNING\n");
        g_temp_ok = false;
        return;
    }
    g_temp_ok = g_safety_ok = cabinet_temp_ok(&reading);
    g_temp_tenths = reading.temperature_tenths;
}

/**
 * @brief Consume one debounced manual raise press and raise the request.
 *
 * A manual raise raises the pass pending indication. It never changes
 * the guarded state on its own, so it cannot silently bypass
 * authorization.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_handle_raise(void) {
    if (!raise_consume_press()) {
        return;
    }
    g_pass_pending = true;
    printf("BUTTON raise request -> pending\n");
}

/**
 * @brief Report whether an infrared command requests a monthly pass.
 *
 * @param command Eight-bit infrared command code.
 * @return bool true when the command raises the pass pending indication.
 */
static bool monitor_ir_requests_pass(uint8_t command) {
    return (command == BARRIER_IR_PASS) || (command == BARRIER_IR_ACK);
}

/**
 * @brief Map a decoded monthly-pass remote command to its name.
 *
 * @param command Decoded NEC command byte.
 * @return const char* Command name string.
 */
static const char *monitor_ir_name(uint8_t command) {
    if (command == BARRIER_IR_PASS) return "PASS";
    if (command == BARRIER_IR_TEST) return "TEST";
    if (command == BARRIER_IR_ACK) return "ACK";
    return "UNKNOWN";
}

/**
 * @brief Apply one decoded infrared monthly-pass remote command.
 *
 * @param cmd Pointer to the decoded infrared command.
 * @return void
 */
static void monitor_apply_ir_command(const ir_command_t *cmd) {
    printf("IR %s (0x%02X)\n", monitor_ir_name(cmd->command), (unsigned)cmd->command);
    if (cmd->command == BARRIER_IR_TEST) {
        return;
    }
    if (monitor_ir_requests_pass(cmd->command)) {
        g_pass_pending = true;
    }
}

/**
 * @brief Poll the infrared monthly-pass remote for a command.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_handle_ir(void) {
    ir_command_t cmd;
    if (!ir_remote_poll(&cmd)) {
        return;
    }
    monitor_apply_ir_command(&cmd);
}

/**
 * @brief Apply one authorized command to the boom and barrier state.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_apply_command(void) {
    g_state = monitor_state_for(control_command());
    g_zone = (int16_t)control_zone();
    g_pass_pending = false;
    monitor_apply_state();
}

/**
 * @brief Verify and apply one inbound barrier frame.
 *
 * The sealed pass, raise, or lower command is authenticated and
 * authorized before it can move the boom. No manual raise can bypass
 * this authorization, and no untrusted task is ever executed.
 *
 * @param hex Pointer to the inbound frame text.
 * @param now_us Current monotonic time in microseconds.
 * @return void
 */
static void monitor_apply_frame(const char *hex, uint64_t now_us) {
    if (!control_handle_frame(hex)) {
        return;
    }
    g_link_seen = true;
    g_last_rx_us = now_us;
    monitor_apply_command();
}

/**
 * @brief Fail safe to the raised posture on a silent control link.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_fail_safe(void) {
    g_state = BARRIER_STATE_DENIED;
    g_zone = 0;
    g_pass_pending = false;
    boom_fail_safe();
}

/**
 * @brief Drain inbound radio lines and apply any sealed command.
 *
 * @param now_us Current monotonic time in microseconds.
 * @return void
 */
static void monitor_rx_tick(uint64_t now_us) {
    radio_rcv_t rcv;
    while (radio_line_pump(BARRIER_UART, g_rx_line, &g_rx_len)) {
        if (radio_parse_rcv(g_rx_line, &rcv) == RADIO_RESULT_OK) {
            printf("RX from 0x%04X, %u bytes\n", (unsigned)rcv.sender, (unsigned)rcv.len);
            monitor_apply_frame(rcv.payload, now_us);
        }
    }
}

/**
 * @brief Drive to the fail-safe raised posture when the control link is silent.
 *
 * @param now_us Current monotonic time in microseconds.
 * @return void
 */
static void monitor_check_link(uint64_t now_us) {
    if (!g_link_seen) {
        return;
    }
    if ((now_us - g_last_rx_us) <= (uint64_t)BARRIER_LINK_WAIT_MS * 1000u) {
        return;
    }
    monitor_fail_safe();
}

/**
 * @brief Service the raise button, monthly-pass remote, and control link.
 *
 * @param now_us Current monotonic time in microseconds.
 * @return void
 */
static void monitor_service_inputs(uint64_t now_us) {
    monitor_handle_raise();
    monitor_handle_ir();
    monitor_rx_tick(now_us);
    monitor_check_link(now_us);
}

/**
 * @brief Advance the SANDBOX_ONLY implant when it is compiled in.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_implant_tick(void) {
#ifdef SANDBOX_ONLY
    implant_tick();
#endif
}

/**
 * @brief Drive exactly one tower light lamp for the current barrier state.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_drive_leds(void) {
    status_led_show(monitor_led_for(g_state));
}

/**
 * @brief Print one live cabinet status line for the interactive console.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_log_reading(void) {
    printf("CAB t=%d ok=%d LED=%d cyc=%u\n", (int)g_temp_tenths, (int)g_temp_ok, (int)monitor_led_for(g_state), (unsigned)g_cycles);
}

/**
 * @brief Drive the tower light, boom, implant, and barrier display.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_service_outputs(void) {
    monitor_implant_tick();
    monitor_sync_weapon();
    boom_tick();
    monitor_drive_leds();
    monitor_heartbeat();
    monitor_render();
    monitor_log_reading();
}

bool monitor_step(void) {
    uint64_t now_us;
    if (!g_ready) {
        return false;
    }
    now_us = time_us_64();
    monitor_refresh_temp();
    monitor_service_inputs(now_us);
    monitor_service_outputs();
    return true;
}
