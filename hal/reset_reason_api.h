/** \addtogroup hal */
/** @{*/
/* mbed Microcontroller Library
 * Copyright (c) 2017 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef MBED_RESET_REASON_API_H
#define MBED_RESET_REASON_API_H

#if DEVICE_RESET_REASON

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  RESET_REASON_POWER_ON  = (1 << 0), /**< Set when power is initially applied to the board. The power-on-reset circuit causes a POWER_ON reset when this occurs */
  RESET_REASON_PIN_RESET = (1 << 1), /**< Set when a reset is triggered by the hardware pin on the board */
  RESET_REASON_BROWN_OUT = (1 << 2), /**< Triggered when the voltage drops below the low voltage detect (LVD) threshold the system will be held in a reset until the voltage rises above the threshold */
  RESET_REASON_SOFTWARE  = (1 << 3), /**< Set during software reset, typically triggered by writing the SYSRESETREQ bit in the Application Interrupt and Reset Control register */
  RESET_REASON_WATCHDOG  = (1 << 4), /**< Set when a running watchdog timer fails to be refreshed */
  RESET_REASON_LOCKUP    = (1 << 5), /**< Set when the core is locked because of an unrecoverable exception */
  RESET_REASON_MULTIPLE  = (1 << 6), /**< Set if multiple reset reasons are set within the board. Occurs when the reset reason registers aren't cleared between resets */
  RESET_REASON_PLATFORM  = (1 << 7), /**< Platform specific reset reason not captured in this enum */
  RESET_REASON_UNKNOWN   = (1 << 8)  /**< Unknown or unreadable reset reason **/
} reset_reason_t;

/**
 * Fetch the reset reason for the last system reset
 *
 * Note: Some platforms contain reset reason registers that persist through
 * system resets. If the registers haven't been cleared before calling this
 * function multiple reasons may be set within the registers. If multiple reset
 * reasons are detected this function will return RESET_REASON_MULTIPLE.
 *
 * @return enum containing the last reset reason for the board.
 */
  reset_reason_t hal_reset_reason_get(void);

  /**
 * Clear the reset reason from registers
 *
 * Reset the value of the reset status registers, the reset reason will persist
 * between system resets on certain platforms so the registers should be cleared
 * before the system resets. Failing to do so may make it difficult to determine
 * the cause of any subsequent system resets.
 */
  void hal_reset_reason_clear(void);

/**@}*/

#ifdef __cplusplus
}
#endif

#endif // DEVICE_RESET_REASON

#endif // MBED_RESET_REASON_API_H

/** @}*/
