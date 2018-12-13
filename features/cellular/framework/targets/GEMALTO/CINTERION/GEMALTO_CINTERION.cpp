/*
 * Copyright (c) 2018, Arm Limited and affiliates.
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

#include "GEMALTO_CINTERION_CellularContext.h"
#include "GEMALTO_CINTERION.h"
#include "AT_CellularNetwork.h"
#include "AT_CellularInformation.h"
#include "CellularLog.h"

using namespace mbed;
using namespace events;

const uint16_t RESPONSE_TO_SEND_DELAY = 100; // response-to-send delay in milliseconds at bit-rate over 9600

GEMALTO_CINTERION::Module GEMALTO_CINTERION::_module;

GEMALTO_CINTERION::GEMALTO_CINTERION(FileHandle *fh) : AT_CellularDevice(fh)
{
}

GEMALTO_CINTERION::~GEMALTO_CINTERION()
{
}

AT_CellularContext *GEMALTO_CINTERION::create_context_impl(ATHandler &at, const char *apn)
{
    return new GEMALTO_CINTERION_CellularContext(at, this, apn);
}

nsapi_error_t GEMALTO_CINTERION::init()
{
    nsapi_error_t err = AT_CellularDevice::init();
    if (err != NSAPI_ERROR_OK) {
        return err;
    }

    CellularInformation *information = open_information();
    if (!information) {
        return NSAPI_ERROR_NO_MEMORY;
    }
    char model[sizeof("ELS61") + 1]; // sizeof need to be long enough to hold just the model text
    nsapi_error_t ret = information->get_model(model, sizeof(model));
    close_information();
    if (ret != NSAPI_ERROR_OK) {
        tr_error("Cellular model not found!");
        return NSAPI_ERROR_DEVICE_ERROR;
    }

    if (memcmp(model, "ELS61", sizeof("ELS61") - 1) == 0) {
        init_module_els61();
    } else if (memcmp(model, "BGS2", sizeof("BGS2") - 1) == 0) {
        init_module_bgs2();
    } else if (memcmp(model, "EMS31", sizeof("EMS31") - 1) == 0) {
        init_module_ems31();
    } else {
        tr_error("Cinterion model unsupported %s", model);
        return NSAPI_ERROR_UNSUPPORTED;
    }
    tr_info("Cinterion model %s (%d)", model, _module);

    return NSAPI_ERROR_OK;
}

uint16_t GEMALTO_CINTERION::get_send_delay() const
{
    return RESPONSE_TO_SEND_DELAY;
}

GEMALTO_CINTERION::Module GEMALTO_CINTERION::get_module()
{
    return _module;
}

void GEMALTO_CINTERION::init_module_bgs2()
{
    // BGS2-W_ATC_V00.100
    static const intptr_t cellular_properties[AT_CellularBase::PROPERTY_MAX] = {
        AT_CellularNetwork::RegistrationModeLAC,  // C_EREG
        AT_CellularNetwork::RegistrationModeLAC,  // C_GREG
        AT_CellularNetwork::RegistrationModeLAC,  // C_REG
        0,  // AT_CGSN_WITH_TYPE
        1,  // AT_CGDATA
        1,  // AT_CGAUTH
        1,  // PROPERTY_IPV4_STACK
        0,  // PROPERTY_IPV6_STACK
        0,  // PROPERTY_IPV4V6_STACK
    };
    AT_CellularBase::set_cellular_properties(cellular_properties);
    _module = ModuleBGS2;
}

void GEMALTO_CINTERION::init_module_els61()
{
    // ELS61-E2_ATC_V01.000
    static const intptr_t cellular_properties[AT_CellularBase::PROPERTY_MAX] = {
        AT_CellularNetwork::RegistrationModeDisable, // C_EREG
        AT_CellularNetwork::RegistrationModeEnable,  // C_GREG
        AT_CellularNetwork::RegistrationModeLAC,     // C_REG
        0,  // AT_CGSN_WITH_TYPE
        1,  // AT_CGDATA
        1,  // AT_CGAUTH
        1,  // PROPERTY_IPV4_STACK
        1,  // PROPERTY_IPV6_STACK
        0,  // PROPERTY_IPV4V6_STACK
    };
    AT_CellularBase::set_cellular_properties(cellular_properties);
    _module = ModuleELS61;
}

void GEMALTO_CINTERION::init_module_ems31()
{
    // EMS31-US_ATC_V4.9.5
    static const intptr_t cellular_properties[AT_CellularBase::PROPERTY_MAX] = {
        AT_CellularNetwork::RegistrationModeLAC,        // C_EREG
        AT_CellularNetwork::RegistrationModeDisable,    // C_GREG
        AT_CellularNetwork::RegistrationModeDisable,    // C_REG
        1,  // AT_CGSN_WITH_TYPE
        1,  // AT_CGDATA
        1,  // AT_CGAUTH
        1,  // PROPERTY_IPV4_STACK
        1,  // PROPERTY_IPV6_STACK
        1,  // PROPERTY_IPV4V6_STACK
    };
    AT_CellularBase::set_cellular_properties(cellular_properties);
    _module = ModuleEMS31;
}
