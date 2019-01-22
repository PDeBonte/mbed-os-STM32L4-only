/* Copyright (c) 2017-2018 ARM Limited
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

#ifndef PSA_PLATFORM_PARTITION_H
#define PSA_PLATFORM_PARTITION_H

#define PLATFORM_ID 8

#define PLATFORM_ROT_SRV_COUNT (2UL)
#define PLATFORM_EXT_ROT_SRV_COUNT (0UL)

/* PLATFORM event flags */
#define PLATFORM_RESERVED1_POS (1UL)
#define PLATFORM_RESERVED1_MSK (1UL << PLATFORM_RESERVED1_POS)

#define PLATFORM_RESERVED2_POS (2UL)
#define PLATFORM_RESERVED2_MSK (1UL << PLATFORM_RESERVED2_POS)



#define PSA_PLATFORM_LC_GET_MSK_POS (4UL)
#define PSA_PLATFORM_LC_GET_MSK (1UL << PSA_PLATFORM_LC_GET_MSK_POS)
#define PSA_PLATFORM_LC_SET_MSK_POS (5UL)
#define PSA_PLATFORM_LC_SET_MSK (1UL << PSA_PLATFORM_LC_SET_MSK_POS)

#define PLATFORM_WAIT_ANY_SID_MSK (\
    PSA_PLATFORM_LC_GET_MSK | \
    PSA_PLATFORM_LC_SET_MSK)


#endif // PSA_PLATFORM_PARTITION_H
