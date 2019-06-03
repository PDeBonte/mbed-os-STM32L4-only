/* Mbed Microcontroller Library
 * Copyright (c) 2018 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
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
#if !DEVICE_WATCHDOG
#error [NOT_SUPPORTED] Watchdog not supported for this target
#endif

#include "greentea-client/test_env.h"
#include "utest/utest.h"
#include "unity/unity.h"
#include "platform/mbed_watchdog_mgr.h"
#include "watchdog_api.h"
#include "watchdog_mgr_reset_tests.h"
#include "mbed.h"

#define TIMEOUT_DELTA_MS 50UL

#define MSG_VALUE_DUMMY "0"
#define CASE_DATA_INVALID 0xffffffffUL
#define CASE_DATA_PHASE2_OK 0xfffffffeUL

#define MSG_VALUE_LEN 24
#define MSG_KEY_LEN 24

#define MSG_KEY_DEVICE_READY "ready"
#define MSG_KEY_START_CASE "start_case"
#define MSG_KEY_DEVICE_RESET "dev_reset"

using utest::v1::Case;
using utest::v1::Specification;
using utest::v1::Harness;

struct testcase_data {
    int index;
    int start_index;
    uint32_t received_data;
};

void release_sem(Semaphore *sem)
{
    sem->release();
}

testcase_data current_case;

bool send_reset_notification(testcase_data *tcdata, uint32_t delay_ms)
{
    char msg_value[12];
    int str_len = snprintf(msg_value, sizeof msg_value, "%02x,%08lx", tcdata->start_index + tcdata->index, delay_ms);
    if (str_len != (sizeof msg_value) - 1) {
        utest_printf("Failed to compose a value string to be sent to host.");
        return false;
    }
    greentea_send_kv(MSG_KEY_DEVICE_RESET, msg_value);
    return true;
}

void test_simple_reset()
{
    // Phase 2. -- verify the test results.
    // Verify if this test case passed based on data received from host.
    if (current_case.received_data != CASE_DATA_INVALID) {
        TEST_ASSERT_EQUAL(CASE_DATA_PHASE2_OK, current_case.received_data);
        current_case.received_data = CASE_DATA_INVALID;
        return;
    }

    // Phase 1. -- run the test code.
    // Init the watchdog and wait for a device reset.
    if (send_reset_notification(&current_case, HW_WATCHDOG_TIMEOUT + TIMEOUT_DELTA_MS) == false) {
        TEST_ASSERT_MESSAGE(0, "Dev-host communication error.");
        return;
    }
    TEST_ASSERT_TRUE(mbed_wdog_manager_start());
    // Block interrupts, including the one from the wdog_manager maintenance ticker.
    core_util_critical_section_enter();
    wait((HW_WATCHDOG_TIMEOUT + TIMEOUT_DELTA_MS) / 1000.0); // Device reset expected.

    // Watchdog reset should have occurred during wait() above;

    core_util_critical_section_exit();
    TEST_ASSERT_MESSAGE(0, "Watchdog did not reset the device as expected.");
}

void test_restart_reset()
{
    watchdog_features_t features = hal_watchdog_get_platform_features();
    if (!features.disable_watchdog) {
        TEST_IGNORE_MESSAGE("Disabling Watchdog not supported for this platform");
        return;
    }

    // Phase 2. -- verify the test results.
    if (current_case.received_data != CASE_DATA_INVALID) {
        TEST_ASSERT_EQUAL(CASE_DATA_PHASE2_OK, current_case.received_data);
        current_case.received_data = CASE_DATA_INVALID;
        return;
    }

    // Phase 1. -- run the test code.
    TEST_ASSERT_TRUE(mbed_wdog_manager_start());
    // The Watchdog Manager maintenance ticker has a period equal to a half of
    // Watchdog timeout. Wait shorter than that and stop the Watchdog Manager
    // before the Watchdog is kicked by the ticker callback.
    wait((HW_WATCHDOG_TIMEOUT / 4UL) / 1000.0);
    TEST_ASSERT_TRUE(mbed_wdog_manager_stop());
    // Block interrupts, including the one from the wdog_manager maintenance ticker.
    core_util_critical_section_enter();
    // Check that stopping the Watchdog Manager prevents a device reset.
    wait((HW_WATCHDOG_TIMEOUT + TIMEOUT_DELTA_MS) / 1000.0);
    core_util_critical_section_exit();

    if (send_reset_notification(&current_case, HW_WATCHDOG_TIMEOUT + TIMEOUT_DELTA_MS) == false) {
        TEST_ASSERT_MESSAGE(0, "Dev-host communication error.");
        return;
    }
    TEST_ASSERT_TRUE(mbed_wdog_manager_start());
    // Block interrupts, including the one from the wdog_manager maintenance ticker.
    core_util_critical_section_enter();
    wait((HW_WATCHDOG_TIMEOUT + TIMEOUT_DELTA_MS) / 1000.0); // Device reset expected.

    // Watchdog reset should have occurred during wait() above;

    core_util_critical_section_exit();
    TEST_ASSERT_MESSAGE(0, "Watchdog did not reset the device as expected.");
}

void test_kick_reset()
{
    // Phase 2. -- verify the test results.
    if (current_case.received_data != CASE_DATA_INVALID) {
        TEST_ASSERT_EQUAL(CASE_DATA_PHASE2_OK, current_case.received_data);
        current_case.received_data = CASE_DATA_INVALID;
        return;
    }

    // Phase 1. -- run the test code.
    TEST_ASSERT_TRUE(mbed_wdog_manager_start());
    wait((HW_WATCHDOG_TIMEOUT + TIMEOUT_DELTA_MS) / 1000.0); // Device reset expected.

    if (send_reset_notification(&current_case, HW_WATCHDOG_TIMEOUT + TIMEOUT_DELTA_MS) == false) {
        TEST_ASSERT_MESSAGE(0, "Dev-host communication error.");
        return;
    }
    // Block interrupts, including the one from the wdog_manager maintenance ticker.
    core_util_critical_section_enter();

    wait((HW_WATCHDOG_TIMEOUT + TIMEOUT_DELTA_MS) / 1000.0); // Device reset expected.

    // Watchdog reset should have occurred during wait() above;

    core_util_critical_section_exit();
    TEST_ASSERT_MESSAGE(0, "Watchdog did not reset the device as expected.");
}

utest::v1::status_t case_setup(const Case *const source, const size_t index_of_case)
{
    current_case.index = index_of_case;
    return utest::v1::greentea_case_setup_handler(source, index_of_case);
}

int testsuite_setup(const size_t number_of_cases)
{
    GREENTEA_SETUP(90, "watchdog_reset");
    utest::v1::status_t status = utest::v1::greentea_test_setup_handler(number_of_cases);
    if (status != utest::v1::STATUS_CONTINUE) {
        return status;
    }

    char key[MSG_KEY_LEN + 1] = { };
    char value[MSG_VALUE_LEN + 1] = { };

    greentea_send_kv(MSG_KEY_DEVICE_READY, MSG_VALUE_DUMMY);
    greentea_parse_kv(key, value, MSG_KEY_LEN, MSG_VALUE_LEN);

    if (strcmp(key, MSG_KEY_START_CASE) != 0) {
        utest_printf("Invalid message key.\n");
        return utest::v1::STATUS_ABORT;
    }

    int num_args = sscanf(value, "%02x,%08lx", &(current_case.start_index), &(current_case.received_data));
    if (num_args == 0 || num_args == EOF) {
        utest_printf("Invalid data received from host\n");
        return utest::v1::STATUS_ABORT;
    }

    utest_printf("This test suite is composed of %i test cases. Starting at index %i.\n", number_of_cases,
                 current_case.start_index);
    return current_case.start_index;
}

Case cases[] = {
    Case("Watchdog Manager reset", case_setup, test_simple_reset),
    Case("Watchdog Manager started again", case_setup, test_restart_reset),
    Case("Watchdog Manager's ticker prevents reset", case_setup, test_kick_reset),
};

Specification specification((utest::v1::test_setup_handler_t) testsuite_setup, cases);

int main()
{
    // Harness will start with a test case index provided by host script.
    return !Harness::run(specification);
}
