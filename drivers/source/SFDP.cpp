/*
 * Copyright (c) 2020, Arm Limited and affiliates.
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

#include <algorithm>
#include <cstdint>
#include <cstring>
#include "drivers/internal/SFDP.h"

#if (DEVICE_SPI || DEVICE_QSPI)

#include "features/frameworks/mbed-trace/mbed-trace/mbed_trace.h"
#define TRACE_GROUP "SFDP"

namespace {

/* Extracts Parameter ID MSB from the second DWORD of a parameter header */
inline uint8_t sfdp_get_param_id_msb(uint32_t dword2)
{
    return (dword2 & 0xFF000000) >> 24;
}

/* Extracts Parameter Table Pointer from the second DWORD of a parameter header */
inline uint32_t sfdp_get_param_tbl_ptr(uint32_t dword2)
{
    return dword2 & 0x00FFFFFF;
}
}

namespace mbed {

/* Verifies SFDP Header and return number of parameter headers */
int sfdp_parse_sfdp_header(sfdp_hdr *sfdp_hdr_ptr)
{
    if (!(memcmp(sfdp_hdr_ptr, "SFDP", 4) == 0 && sfdp_hdr_ptr->R_MAJOR == 1)) {
        tr_error("verify SFDP signature and version Failed");
        return -1;
    }

    tr_debug("init - verified SFDP Signature and version Successfully");

    int hdr_cnt = sfdp_hdr_ptr->NPH + 1;
    tr_debug("number of Param Headers: %d", hdr_cnt);

    return hdr_cnt;
}

int sfdp_parse_single_param_header(sfdp_prm_hdr *phdr, sfdp_hdr_info &hdr_info)
{
    if (phdr->P_MAJOR != 1) {
        tr_error("Param Header: - Major Version should be 1!");
        return -1;
    }

    if ((phdr->PID_LSB == 0) && (sfdp_get_param_id_msb(phdr->DWORD2) == 0xFF)) {
        tr_debug("Parameter Header: Basic Parameter Header");
        hdr_info.bptbl.addr = sfdp_get_param_tbl_ptr(phdr->DWORD2);
        hdr_info.bptbl.size = std::min((phdr->P_LEN * 4), SFDP_BASIC_PARAMS_TBL_SIZE);

    } else if ((phdr->PID_LSB == 0x81) && (sfdp_get_param_id_msb(phdr->DWORD2) == 0xFF)) {
        tr_debug("Parameter Header: Sector Map Parameter Header");
        hdr_info.smtbl.addr = sfdp_get_param_tbl_ptr(phdr->DWORD2);
        hdr_info.smtbl.size = phdr->P_LEN * 4;

    } else {
        tr_debug("Parameter Header vendor specific or unknown. Parameter ID LSB: 0x%" PRIX8 "; MSB: 0x%" PRIX8 "",
                 phdr->PID_LSB,
                 sfdp_get_param_id_msb(phdr->DWORD2));
    }

    return 0;
}

int sfdp_parse_headers(Callback<int(bd_addr_t, void *, bd_size_t)> sfdp_reader, sfdp_hdr_info &hdr_info)
{
    bd_addr_t addr = 0x0;
    int number_of_param_headers = 0;
    size_t data_length;

    {
        data_length = SFDP_HEADER_SIZE;
        uint8_t sfdp_header[SFDP_HEADER_SIZE];

        int status = sfdp_reader(addr, sfdp_header, data_length);
        if (status < 0) {
            tr_error("retrieving SFDP Header failed");
            return -1;
        }

        number_of_param_headers = sfdp_parse_sfdp_header((sfdp_hdr *)sfdp_header);
        if (number_of_param_headers < 0) {
            return number_of_param_headers;
        }
    }

    addr += SFDP_HEADER_SIZE;

    {
        data_length = SFDP_HEADER_SIZE;
        uint8_t param_header[SFDP_HEADER_SIZE];
        int status;
        int hdr_status;

        // Loop over Param Headers and parse them (currently supports Basic Param Table and Sector Region Map Table)
        for (int i_ind = 0; i_ind < number_of_param_headers; i_ind++) {
            status = sfdp_reader(addr, param_header, data_length);
            if (status < 0) {
                tr_error("retrieving Parameter Header %d failed", i_ind + 1);
                return -1;
            }

            hdr_status = sfdp_parse_single_param_header((sfdp_prm_hdr *)param_header, hdr_info);
            if (hdr_status < 0) {
                return hdr_status;
            }

            addr += SFDP_HEADER_SIZE;
        }
    }

    return 0;
}

} /* namespace mbed */
#endif /* (DEVICE_SPI || DEVICE_QSPI) */
