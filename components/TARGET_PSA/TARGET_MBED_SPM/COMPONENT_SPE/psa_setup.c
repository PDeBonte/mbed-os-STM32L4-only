/* Copyright (c) 2017-2019 ARM Limited
 *
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

/***********************************************************************************************************************
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * THIS FILE IS AN AUTO-GENERATED FILE - DO NOT MODIFY IT.
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * Template Version 1.0
 * Generated by tools/spm/generate_partition_code.py Version 1.0
 **********************************************************************************************************************/

#include "spm_panic.h"
#include "spm_internal.h"
#include "handles_manager.h"
#include "cmsis.h"
#include "psa_attest_srv_partition.h"
#include "psa_crypto_srv_partition.h"
#include "psa_platform_partition.h"
#include "psa_its_partition.h"

extern const uint32_t attest_srv_external_sids[7];
extern const uint32_t crypto_srv_external_sids[4];
extern const uint32_t platform_external_sids[1];

__attribute__((weak))
spm_partition_t g_partitions[4] = {
    {
        .partition_id = ATTEST_SRV_ID,
        .thread_id = 0,
        .flags_rot_srv = ATTEST_SRV_WAIT_ANY_SID_MSK,
        .flags_interrupts = 0,
        .rot_services = NULL,
        .rot_services_count = ATTEST_SRV_ROT_SRV_COUNT,
        .extern_sids = attest_srv_external_sids,
        .extern_sids_count = ATTEST_SRV_EXT_ROT_SRV_COUNT,
        .irq_mapper = NULL,
    },
    {
        .partition_id = CRYPTO_SRV_ID,
        .thread_id = 0,
        .flags = CRYPTO_SRV_WAIT_ANY_SID_MSK | CRYPTO_SRV_WAIT_ANY_IRQ_MSK,
        .rot_services = NULL,
        .rot_services_count = CRYPTO_SRV_ROT_SRV_COUNT,
        .extern_sids = crypto_srv_external_sids,
        .extern_sids_count = CRYPTO_SRV_EXT_ROT_SRV_COUNT,
        .irq_mapper = NULL,
    },
    {
        .partition_id = PLATFORM_ID,
        .thread_id = 0,
        .flags = PLATFORM_WAIT_ANY_SID_MSK | PLATFORM_WAIT_ANY_IRQ_MSK,
        .rot_services = NULL,
        .rot_services_count = PLATFORM_ROT_SRV_COUNT,
        .extern_sids = platform_external_sids,
        .extern_sids_count = PLATFORM_EXT_ROT_SRV_COUNT,
        .irq_mapper = NULL,
    },
    {
        .partition_id = ITS_ID,
        .thread_id = 0,
        .flags = ITS_WAIT_ANY_SID_MSK | ITS_WAIT_ANY_IRQ_MSK,
        .rot_services = NULL,
        .rot_services_count = ITS_ROT_SRV_COUNT,
        .extern_sids = NULL,
        .extern_sids_count = ITS_EXT_ROT_SRV_COUNT,
        .irq_mapper = NULL,
    },
};

/* Check all the defined memory regions for overlapping. */

/* A list of all the memory regions. */
__attribute__((weak))
const mem_region_t *mem_regions = NULL;

__attribute__((weak))
const uint32_t mem_region_count = 0;

// forward declaration of partition initializers
void attest_srv_init(spm_partition_t *partition);
void crypto_srv_init(spm_partition_t *partition);
void platform_init(spm_partition_t *partition);
void its_init(spm_partition_t *partition);

__attribute__((weak))
uint32_t init_partitions(spm_partition_t **partitions)
{
    if (NULL == partitions) {
        SPM_PANIC("partitions is NULL!\n");
    }

    attest_srv_init(&(g_partitions[0]));
    crypto_srv_init(&(g_partitions[1]));
    platform_init(&(g_partitions[2]));
    its_init(&(g_partitions[3]));

    *partitions = g_partitions;
    return 4;
}

