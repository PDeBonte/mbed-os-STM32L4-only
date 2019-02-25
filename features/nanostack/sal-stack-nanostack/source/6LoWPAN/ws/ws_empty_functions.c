/*
 * Copyright (c) 2018-2019, Arm Limited and affiliates.
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

#include <string.h>
#include "nsconfig.h"
#include "ns_types.h"
#include "ns_trace.h"
#include <ns_list.h>
#include <nsdynmemLIB.h>
#include "NWK_INTERFACE/Include/protocol.h"
#include "6LoWPAN/ws/ws_common.h"

#include "ws_management_api.h"

#ifndef HAVE_WS
int ws_management_node_init(
    int8_t interface_id,
    uint8_t regulatory_domain,
    char *network_name_ptr,
    fhss_timer_t *fhss_timer_ptr)
{
    (void)interface_id;
    (void)regulatory_domain;
    (void)network_name_ptr;
    (void)fhss_timer_ptr;
    return -1;
}

int ws_management_regulatory_domain_set(
    int8_t interface_id,
    uint8_t regulatory_domain,
    uint8_t operating_class,
    uint8_t operating_mode)
{
    (void)interface_id;
    (void)regulatory_domain;
    (void)operating_class;
    (void)operating_mode;
    return -1;
}

int ws_management_network_size_set(
    int8_t interface_id,
    uint8_t network_size)
{
    (void)interface_id;
    (void)network_size;
    return -1;
}

int ws_management_channel_mask_set(
    int8_t interface_id,
    uint32_t channel_mask[8])
{
    (void)interface_id;
    (void)channel_mask;
    return -1;
}

int ws_management_channel_plan_set(
    int8_t interface_id,
    uint8_t channel_plan,
    uint8_t uc_channel_function,
    uint8_t bc_channel_function,
    uint32_t ch0_freq, // Stack can not modify this
    uint8_t channel_spacing,// Stack can not modify this
    uint8_t number_of_channels)
{
    (void)interface_id;
    (void)channel_plan;
    (void)uc_channel_function;
    (void)bc_channel_function;
    (void)ch0_freq;
    (void)channel_spacing;
    (void)number_of_channels;
    return -1;
}

int ws_management_fhss_timing_configure(
    int8_t interface_id,
    uint8_t fhss_uc_dwell_interval,
    uint32_t fhss_broadcast_interval,
    uint8_t fhss_bc_dwell_interval)
{
    (void)interface_id;
    (void)fhss_uc_dwell_interval;
    (void)fhss_broadcast_interval;
    (void)fhss_bc_dwell_interval;
    return -1;
}

int ws_management_fhss_unicast_channel_function_configure(
    int8_t interface_id,
    uint8_t channel_function,
    uint16_t fixed_channel,
    uint8_t dwell_interval)
{
    (void)interface_id;
    (void)channel_function;
    (void)fixed_channel;
    (void)dwell_interval;
    return -1;
}

int ws_management_fhss_broadcast_channel_function_configure(
    int8_t interface_id,
    uint8_t channel_function,
    uint16_t fixed_channel,
    uint8_t dwell_interval,
    uint32_t broadcast_interval)
{
    (void)interface_id;
    (void)channel_function;
    (void)fixed_channel;
    (void)dwell_interval;
    (void)broadcast_interval;
    return -1;
}

/* ### test api ### */
int ws_test_pan_size_set(int8_t interface_id, uint16_t pan_size)
{
    (void) interface_id;
    (void) pan_size;
    return -1;
}
int ws_test_max_child_count_set(int8_t interface_id, uint16_t child_count)
{
    (void) interface_id;
    (void) child_count;
    return -1;
}

int ws_test_gtk_set(int8_t interface_id, uint8_t *gtk[4])
{
    (void) interface_id;
    (void) gtk;

    return -1;
}

int ws_test_active_key_set(int8_t interface_id, uint8_t index)
{
    (void) interface_id;
    (void) index;

    return -1;
}

int ws_test_key_lifetime_set(int8_t interface_id, uint32_t gtk_lifetime, uint32_t pmk_lifetime, uint32_t ptk_lifetime)
{
    (void) interface_id;
    (void) gtk_lifetime;
    (void) pmk_lifetime;
    (void) ptk_lifetime;

    return -1;
}

int ws_test_gtk_time_settings_set(int8_t interface_id, uint8_t revocat_lifetime_reduct, uint8_t new_activation_time, uint32_t max_mismatch)
{
    (void) interface_id;
    (void) revocat_lifetime_reduct;
    (void) new_activation_time;
    (void) max_mismatch;

    return -1;
}

#endif // no HAVE_WS

