/* mbed Microcontroller Library
 * Copyright (c) 2006-2020 ARM Limited
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

#include <algorithm>
#include <cstdint>

#include "ble/Gap.h"
#include "ble/SecurityManager.h"

#include "source/BLEInstanceBase.h"
#include "source/generic/GapImpl.h"

#include "source/pal/PalGap.h"
#include "source/pal/GapEvents.h"
#include "source/pal/GapTypes.h"
#include "source/pal/PalGenericAccessService.h"
#include "source/pal/PalEventQueue.h"
#include "source/pal/PalSecurityManager.h"


using namespace std::chrono;

MBED_STATIC_ASSERT(BLE_GAP_MAX_ADVERTISING_SETS < 0xFF, "BLE_GAP_MAX_ADVERTISING_SETS must be less than 255");
MBED_STATIC_ASSERT(BLE_GAP_MAX_ADVERTISING_SETS > 0, "BLE_GAP_MAX_ADVERTISING_SETS must be a positive number");

namespace ble {
namespace impl {

namespace {

// Constants
const uint16_t scan_interval_min = 0x0004;
const uint16_t scan_interval_max = 0x4000;
const uint16_t connection_interval_min = 0x0006;
const uint16_t connection_interval_max = 0x0C80;
const uint16_t slave_latency_min = 0x0000;
const uint16_t slave_latency_max = 0x01F3;
const uint16_t advertising_interval_min = 0x0020;
const uint16_t advertising_interval_max = 0x4000;
const uint16_t supervision_timeout_min = 0x000A;
const uint16_t supervision_timeout_max = 0x0C80;

const mbed_error_status_t mixed_scan_api_error =
    MBED_MAKE_ERROR(MBED_MODULE_BLE, MBED_ERROR_CODE_BLE_USE_INCOMPATIBLE_API);

const mbed_error_status_t illegal_state_error =
    MBED_MAKE_ERROR(MBED_MODULE_BLE, MBED_ERROR_CODE_BLE_ILLEGAL_STATE);

/*
 * Return true if value is included in the range [lower_bound : higher_bound]
 */
template<typename T>
static bool is_in_range(T value, T lower_bound, T higher_bound)
{
    if (value < lower_bound || value > higher_bound) {
        return false;
    }
    return true;
}

/*
 * Return true of the connection parameters are acceptable as preferred connection
 * parameters.
 *
 * Preferred connection parameters unlike actual connection parameters allow the
 * max connection interval, min connection interval and connection supervision
 * timeout to be equal to 0xFFFF. When it is the case that value can be
 * interpreted as "non specific".
 */
bool is_preferred_connection_params_valid(const Gap::PreferredConnectionParams_t *params)
{
    if (params == nullptr) {
        return false;
    }

    if (!is_in_range(params->slaveLatency, slave_latency_min, slave_latency_max) == false) {
        return false;
    }

    if ((is_in_range(params->maxConnectionInterval, connection_interval_min, connection_interval_max) == false) &&
        (params->maxConnectionInterval != 0xFFFF)) {
        return false;
    }

    if ((is_in_range(params->minConnectionInterval, connection_interval_min, params->maxConnectionInterval) == false) &&
        (params->minConnectionInterval != 0xFFFF)) {
        return false;
    }

    if (params->connectionSupervisionTimeout == 0xFFFF) {
        return true;
    }

    if ((
        is_in_range(params->connectionSupervisionTimeout, supervision_timeout_min, supervision_timeout_max) ==
            false
    )) {
        return false;
    }

    if (params->maxConnectionInterval == 0xFFFF) {
        return true;
    }

    uint16_t max_connection_interval_ms =
        ((uint32_t) params->maxConnectionInterval * 125) / 100;
    uint16_t min_connection_supervision_timeout =
        ((1 + params->slaveLatency) * max_connection_interval_ms * 2) / 10;

    if (params->connectionSupervisionTimeout < min_connection_supervision_timeout) {
        return false;
    }

    return true;
}

/**
 * Check if random bytes of an address are valid.
 */
bool is_prand_valid(const uint8_t *bytes, size_t len)
{
    // at least one bit of the random part of the static address shall be
    // equal to 0 and at least one bit of the random part of the static
    // address shall be equal to 1
    for (size_t i = 0; i < (len - 1); ++i) {
        if ((bytes[i] != 0x00) && (bytes[i] != 0xFF)) {
            return true;
        }

        if ((i > 0) && (bytes[i] != bytes[i - 1])) {
            return true;
        }
    }

    if (((bytes[len - 1] & 0x3F) == 0x3F) && (bytes[len - 2] == 0xFF)) {
        return false;
    }

    if (((bytes[len - 1] & 0x3F) == 0x00) && (bytes[len - 2] == 0x00)) {
        return false;
    }

    return true;
}

/*
 * Check if the random part of a random address with 48 random bytes are valid
 * or not.
 * Return true if it is the case and false otherwise.
 */
bool is_prand_48_bits_valid(const address_t &address)
{
    return is_prand_valid(address.data(), 6);
}

/*
 * Check if the random part of a random address with 24 random bytes are valid
 * or not.
 * Return true if it is the case and false otherwise.
 */
bool is_prand_24_bits_valid(const address_t &address)
{
    return is_prand_valid(address.data() + 3, 3);
}

/*
 * Return true if address is a random static address.
 */
bool is_random_static_address(const address_t &address)
{
    // top two msb bits shall be equal to 0b11.
    if ((address[5] & 0xC0) != 0xC0) {
        return false;
    }

    return is_prand_48_bits_valid(address);
}

/*
 * Return true if address is a random private non resolvable address.
 */
bool is_random_private_non_resolvable_address(
    const address_t &address
)
{
    // top two msb bits shall be equal to 0b00.
    if ((address[5] & 0xC0) != 0x00) {
        return false;
    }

    return is_prand_48_bits_valid(address);
}

/*
 * Return true if address is a random private resolvable address.
 */
bool is_random_private_resolvable_address(
    const address_t &address
)
{
    // top two msb bits shall be equal to 0b01.
    if ((address[5] & 0xC0) != 0x40) {
        return false;
    }

    return is_prand_24_bits_valid(address);
}

/*
 * Return true if the address is a random address.
 */
bool is_random_address(const address_t &address)
{
    return is_random_private_resolvable_address(address) ||
        is_random_private_non_resolvable_address(address) ||
        is_random_static_address(address);
}

/*
 * Return true if the whitelist in input is valid or false otherwise.
 */
bool is_whitelist_valid(const ::ble::whitelist_t &whitelist)
{
    if (whitelist.size > whitelist.capacity) {
        return false;
    }

    for (size_t i = 0; i < whitelist.size; ++i) {
        const whitelist_t::entry_t &entry = whitelist.addresses[i];
        switch (entry.type.value()) {
            case peer_address_type_t::PUBLIC:
            case peer_address_type_t::PUBLIC_IDENTITY:
                if (is_random_address(entry.address)) {
                    return false; // needs to be public
                }
                break;
            case peer_address_type_t::RANDOM:
            case peer_address_type_t::RANDOM_STATIC_IDENTITY:
                if (!is_random_static_address(entry.address)) {
                    return false; // needs to be random static
                }
                break;
            default: // ANONYMOUS
                return false;
        }

    }
    return true;
}

/*
 * Return true if device is present in the whitelist.
 */
bool is_in_whitelist(
    const whitelist_t::entry_t &device, const whitelist_t &whitelist
)
{
    for (size_t i = 0; i < whitelist.size; ++i) {
        const whitelist_t::entry_t &potential_device = whitelist.addresses[i];

        if (potential_device.address == device.address) {
            return true;
        }
    }
    return false;
}

/*
 * Convert a peer_address_type_t into a whitelist_address_type_t.
 */
whitelist_address_type_t to_whitelist_address_type(
    peer_address_type_t address_type
)
{
    return (
        address_type == peer_address_type_t::PUBLIC
            || address_type == peer_address_type_t::PUBLIC_IDENTITY
    ) ?
        whitelist_address_type_t::PUBLIC_DEVICE_ADDRESS :
        whitelist_address_type_t::RANDOM_DEVICE_ADDRESS;
}

microsecond_t minSupervisionTimeout(
    const conn_interval_t &maxConnectionInterval,
    const slave_latency_t &slaveLatency
)
{
    // see BLUETOOTH SPECIFICATION Version 5.0 | Vol 2, Part E
    // Section 7.8.12 LE Create Connection
    return (1 + slaveLatency.value()) * maxConnectionInterval * 2;
}

} // end of anonymous namespace

const peripheral_privacy_configuration_t Gap::default_peripheral_privacy_configuration = {
    /* use_non_resolvable_random_address */ false,
    /* resolution_strategy */ peripheral_privacy_configuration_t::PERFORM_PAIRING_PROCEDURE
};


const central_privacy_configuration_t Gap::default_central_privacy_configuration = {
    /* use_non_resolvable_random_address */ false,
    /* resolution_strategy */ central_privacy_configuration_t::RESOLVE_AND_FORWARD
};

Gap::Gap(
    ble::PalEventQueue &event_queue,
    ble::PalGap &pal_gap,
    ble::PalGenericAccessService &generic_access_service,
    ble::PalSecurityManager &pal_sm
) : _event_queue(event_queue),
    _pal_gap(pal_gap),
    _gap_service(generic_access_service),
    _pal_sm(pal_sm),
    _address_type(own_address_type_t::PUBLIC),
    _initiator_policy_mode(initiator_policy_t::NO_FILTER),
    _scanning_filter_policy(scanning_filter_policy_t::NO_FILTER),
    _advertising_filter_policy(advertising_filter_policy_t::NO_FILTER),
    _whitelist(),
    _privacy_enabled(false),
    _peripheral_privacy_configuration(default_peripheral_privacy_configuration),
    _central_privacy_configuration(default_central_privacy_configuration),
    _random_address_rotating(false),
    _scan_enabled(false),
    _advertising_timeout(),
    _scan_timeout(),
    _user_manage_connection_parameter_requests(false)
{
    _pal_gap.initialize();

    _pal_gap.when_gap_event_received(
        mbed::callback(this, &Gap::on_gap_event_received)
    );

    // Recover static random identity
    _random_static_identity_address = _pal_gap.get_random_address();

    _pal_gap.set_event_handler(this);
}


Gap::~Gap() = default;


bool Gap::isFeatureSupported(controller_supported_features_t feature)
{
#if !BLE_FEATURE_PHY_MANAGEMENT
    if (feature == ble::controller_supported_features_t::LE_CODED_PHY ||
        feature == ble::controller_supported_features_t::LE_2M_PHY
        ) {
        return false;
    }
#endif

#if !BLE_FEATURE_EXTENDED_ADVERTISING
    if (feature == ble::controller_supported_features_t::LE_EXTENDED_ADVERTISING) {
        return false;
    }
#endif

#if !BLE_FEATURE_PERIODIC_ADVERTISING
    if (feature == ble::controller_supported_features_t::LE_PERIODIC_ADVERTISING) {
        return false;
    }
#endif

#if !BLE_FEATURE_PRIVACY
    if (feature == ble::controller_supported_features_t::LL_PRIVACY) {
        return false;
    }
#endif

    return _pal_gap.is_feature_supported(feature);
}


ble_error_t Gap::setRandomStaticAddress(
    const ble::address_t &address
)
{
    if (is_random_static_address(address.data()) == false) {
        return BLE_ERROR_INVALID_PARAM;
    }

    ble_error_t err = _pal_gap.set_random_address(address);
    if (err) {
        return err;
    }

    _address_type = own_address_type_t::RANDOM;
    _address = address;
    _random_static_identity_address = address;
    return BLE_ERROR_NONE;
}


ble_error_t Gap::getAddress(
    own_address_type_t &type,
    address_t &address
)
{
    type = _address_type;

    if (_address_type == own_address_type_t::PUBLIC) {
        address = _pal_gap.get_device_address();
    } else {
        address = _pal_gap.get_random_address();
    }

    return BLE_ERROR_NONE;
}


ble_error_t Gap::getRandomAddressType(
    const address_t address,
    ble::random_address_type_t *type
)
{
    // see section Device address in Bluetooth Link Layer specification
    // (Vol 6 - Part B)
    switch (address[5] >> 6) {
        case 0x03:
            *type = random_address_type_t::STATIC;
            return BLE_ERROR_NONE;
        case 0x00:
            *type = random_address_type_t::NON_RESOLVABLE_PRIVATE;
            return BLE_ERROR_NONE;
        case 0x01:
            *type = random_address_type_t::RESOLVABLE_PRIVATE;
            return BLE_ERROR_NONE;
        default:
            return BLE_ERROR_INVALID_PARAM;
    }
}


ble_error_t Gap::stopScan()
{
    ble_error_t err;

    if (!_scan_enabled) {
        return BLE_ERROR_NONE;
    }

    _scan_enabled = false;

    if (is_extended_advertising_available()) {
        err = _pal_gap.extended_scan_enable(false, duplicates_filter_t::DISABLE, 0, 0);
    } else {
        err = _pal_gap.scan_enable(false, false);
    }

    if (err) {
        _scan_enabled = true;
        return err;
    }

#if BLE_FEATURE_PRIVACY
    // Stop address rotation if required
    set_random_address_rotation(false);
#endif

    _scan_timeout.detach();

    return BLE_ERROR_NONE;
}


ble_error_t Gap::connect(
    peer_address_type_t peerAddressType,
    const ble::address_t &peerAddress,
    const ConnectionParameters &connectionParams
)
{
    if (!connectionParams.getNumberOfEnabledPhys()) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (peerAddressType == peer_address_type_t::ANONYMOUS) {
        return BLE_ERROR_INVALID_PARAM;
    }

    for (size_t i = 0; i < connectionParams.getPhySet().count(); ++i) {
        if (supervision_timeout_t(connectionParams.getConnectionSupervisionTimeoutArray()[i]) <=
            minSupervisionTimeout(
                conn_interval_t(connectionParams.getMaxConnectionIntervalArray()[i]),
                slave_latency_t(connectionParams.getSlaveLatencyArray()[i])
            )
            ) {
            return BLE_ERROR_INVALID_PARAM;
        }
    }

    if (is_extended_advertising_available() == false) {
        phy_set_t set(connectionParams.getPhySet());
        if (set.count() != 1 || set.get_1m() == false) {
            return BLE_ERROR_INVALID_PARAM;
        }

        // ensure scan is stopped.
        _pal_gap.scan_enable(false, false);

        return _pal_gap.create_connection(
            connectionParams.getScanIntervalArray()[0],
            connectionParams.getScanWindowArray()[0],
            connectionParams.getFilter(),
            (connection_peer_address_type_t::type) peerAddressType.value(),
            peerAddress,
            connectionParams.getOwnAddressType(),
            connectionParams.getMinConnectionIntervalArray()[0],
            connectionParams.getMaxConnectionIntervalArray()[0],
            connectionParams.getSlaveLatencyArray()[0],
            connectionParams.getConnectionSupervisionTimeoutArray()[0],
            connectionParams.getMinEventLengthArray()[0],
            connectionParams.getMaxConnectionIntervalArray()[0]
        );
    }

    // ensure scan is stopped.
    _pal_gap.extended_scan_enable(false, duplicates_filter_t::DISABLE, 0, 0);

    // reduce the address type to public or random
    peer_address_type_t adjusted_address_type(peer_address_type_t::PUBLIC);

    if (peerAddressType == peer_address_type_t::RANDOM ||
        peerAddressType == peer_address_type_t::RANDOM_STATIC_IDENTITY
        ) {
        adjusted_address_type = peer_address_type_t::RANDOM;
    }

    return _pal_gap.extended_create_connection(
        connectionParams.getFilter(),
        connectionParams.getOwnAddressType(),
        adjusted_address_type,
        peerAddress,
        connectionParams.getPhySet(),
        connectionParams.getScanIntervalArray(),
        connectionParams.getScanWindowArray(),
        connectionParams.getMinConnectionIntervalArray(),
        connectionParams.getMaxConnectionIntervalArray(),
        connectionParams.getSlaveLatencyArray(),
        connectionParams.getConnectionSupervisionTimeoutArray(),
        connectionParams.getMinEventLengthArray(),
        connectionParams.getMaxEventLengthArray()
    );
}


ble_error_t Gap::manageConnectionParametersUpdateRequest(bool flag)
{
    _user_manage_connection_parameter_requests = flag;
    return BLE_ERROR_NONE;
}


ble_error_t Gap::updateConnectionParameters(
    connection_handle_t connectionHandle,
    conn_interval_t minConnectionInterval,
    conn_interval_t maxConnectionInterval,
    slave_latency_t slaveLatency,
    supervision_timeout_t supervisionTimeout,
    conn_event_length_t minConnectionEventLength,
    conn_event_length_t maxConnectionEventLength
)
{
    if (supervisionTimeout <= minSupervisionTimeout(maxConnectionInterval, slaveLatency)) {
        return BLE_ERROR_INVALID_PARAM;
    }

    return _pal_gap.connection_parameters_update(
        connectionHandle,
        minConnectionInterval.value(),
        maxConnectionInterval.value(),
        slaveLatency.value(),
        supervisionTimeout.value(),
        minConnectionEventLength.value(),
        maxConnectionEventLength.value()
    );
}


ble_error_t Gap::acceptConnectionParametersUpdate(
    connection_handle_t connectionHandle,
    conn_interval_t minConnectionInterval,
    conn_interval_t maxConnectionInterval,
    slave_latency_t slaveLatency,
    supervision_timeout_t supervisionTimeout,
    conn_event_length_t minConnectionEventLength,
    conn_event_length_t maxConnectionEventLength
)
{
    if (supervisionTimeout <= minSupervisionTimeout(maxConnectionInterval, slaveLatency)) {
        return BLE_ERROR_INVALID_PARAM;
    }

    return _pal_gap.accept_connection_parameter_request(
        connectionHandle,
        minConnectionInterval.value(),
        maxConnectionInterval.value(),
        slaveLatency.value(),
        supervisionTimeout.value(),
        minConnectionEventLength.value(),
        maxConnectionEventLength.value()
    );
}


ble_error_t Gap::rejectConnectionParametersUpdate(
    connection_handle_t connectionHandle
)
{
    return _pal_gap.reject_connection_parameter_request(
        connectionHandle,
        hci_error_code_t::UNACCEPTABLE_CONNECTION_PARAMETERS
    );
}


ble_error_t Gap::cancelConnect()
{
    return _pal_gap.cancel_connection_creation();
}

ble_error_t Gap::readPhy(ble::connection_handle_t connection)
{
    return _pal_gap.read_phy(connection);
}


ble_error_t Gap::setPreferredPhys(
    const phy_set_t *txPhys,
    const phy_set_t *rxPhys
)
{
    phy_set_t tx_phys(txPhys ? txPhys->value() : 0);
    phy_set_t rx_phys(rxPhys ? rxPhys->value() : 0);
    return _pal_gap.set_preferred_phys(tx_phys, rx_phys);
}


ble_error_t Gap::setPhy(
    ble::connection_handle_t connection,
    const phy_set_t *txPhys,
    const phy_set_t *rxPhys,
    ble::coded_symbol_per_bit_t codedSymbol
)
{
    phy_set_t tx_phys(txPhys ? txPhys->value() : 0);
    phy_set_t rx_phys(rxPhys ? rxPhys->value() : 0);
    return _pal_gap.set_phy(connection, tx_phys, rx_phys, codedSymbol);
}


void Gap::on_read_phy(
    hci_error_code_t hci_status,
    ble::connection_handle_t connection_handle,
    phy_t tx_phy,
    phy_t rx_phy
)
{
    ble_error_t status = BLE_ERROR_NONE;
    if (hci_status != hci_error_code_t::SUCCESS) {
        status = BLE_ERROR_UNSPECIFIED;
    }

    if (_event_handler) {
        _event_handler->onPhyUpdateComplete(status, connection_handle, tx_phy, rx_phy);
    }
}


void Gap::on_data_length_change(
    connection_handle_t connection_handle,
    uint16_t tx_size,
    uint16_t rx_size
)
{
    if (_event_handler) {
        _event_handler->onDataLengthChange(connection_handle, tx_size, rx_size);
    }
}


void Gap::on_phy_update_complete(
    hci_error_code_t hci_status,
    ble::connection_handle_t connection_handle,
    phy_t tx_phy,
    phy_t rx_phy
)
{
    ble_error_t status = BLE_ERROR_NONE;
    if (hci_status != hci_error_code_t::SUCCESS) {
        status = BLE_ERROR_UNSPECIFIED;
    }

    if (_event_handler) {
        _event_handler->onPhyUpdateComplete(status, connection_handle, tx_phy, rx_phy);
    }
}


ble_error_t Gap::disconnect(
    connection_handle_t connectionHandle,
    local_disconnection_reason_t reason
)
{
    return _pal_gap.disconnect(connectionHandle, reason);
}


uint8_t Gap::getMaxWhitelistSize(void) const
{
    return _pal_gap.read_white_list_capacity();
}


ble_error_t Gap::getWhitelist(whitelist_t &whitelist) const
{
    if (initialize_whitelist() == false) {
        return BLE_ERROR_INVALID_STATE;
    }

    if (whitelist.capacity < _whitelist.capacity) {
        return BLE_ERROR_INVALID_PARAM;
    }

    for (size_t i = 0; i < _whitelist.size; ++i) {
        whitelist.addresses[i] = _whitelist.addresses[i];
    }

    whitelist.capacity = _whitelist.capacity;
    return BLE_ERROR_NONE;
}


ble_error_t Gap::setWhitelist(const whitelist_t &whitelist)
{
    if (is_whitelist_valid(whitelist) == false) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (initialize_whitelist() == false) {
        return BLE_ERROR_INVALID_STATE;
    }

    if (whitelist.capacity > _whitelist.capacity) {
        return BLE_ERROR_INVALID_PARAM;
    }

    // first evict devices not in the existing whitelist
    for (size_t i = 0; i < _whitelist.size; ++i) {
        const whitelist_t::entry_t &device = _whitelist.addresses[i];

        if (is_in_whitelist(device, whitelist) == false) {
            ble_error_t err = _pal_gap.remove_device_from_whitelist(
                to_whitelist_address_type(device.type),
                device.address
            );

            // try to restore the whitelist to its initial state
            if (err) {
                for (size_t j = 0; j < i; ++j) {
                    const whitelist_t::entry_t &device = _whitelist.addresses[j];

                    if (is_in_whitelist(device, whitelist) == false) {
                        _pal_gap.add_device_to_whitelist(
                            to_whitelist_address_type(device.type),
                            device.address
                        );
                    }
                }
                return err;
            }
        }
    }

    // second add devices which were not in the initial whitelist
    for (size_t i = 0; i < whitelist.size; ++i) {
        const whitelist_t::entry_t &device = whitelist.addresses[i];

        if (is_in_whitelist(device, _whitelist) == false) {
            ble_error_t err = _pal_gap.add_device_to_whitelist(
                to_whitelist_address_type(device.type),
                device.address
            );

            // try to restore the whitelist to its initial state
            if (err) {
                // first remove the devices added
                for (size_t j = 0; j < i; ++j) {
                    const whitelist_t::entry_t &device = whitelist.addresses[j];

                    if (is_in_whitelist(device, _whitelist) == false) {
                        _pal_gap.remove_device_from_whitelist(
                            to_whitelist_address_type(device.type),
                            device.address
                        );
                    }
                }

                // second add the devices of the initial list evicted
                for (size_t i = 0; i < _whitelist.size; ++i) {
                    const whitelist_t::entry_t &device = _whitelist.addresses[i];

                    if (is_in_whitelist(device, whitelist) == false) {
                        _pal_gap.add_device_to_whitelist(
                            to_whitelist_address_type(device.type),
                            device.address
                        );
                    }
                }

                return err;
            }
        }
    }

    // commit the devices into the whitelist
    for (size_t i = 0; i < whitelist.size; ++i) {
        _whitelist.addresses[i] = whitelist.addresses[i];
    }

    _whitelist.size = whitelist.size;

    return BLE_ERROR_NONE;
}


ble_error_t Gap::enablePrivacy(bool enable)
{
    if (enable == _privacy_enabled) {
        // No change
        return BLE_ERROR_NONE;
    }

    if (enable && !_pal_gap.is_privacy_supported()) {
        // Privacy is not supported by the implementation
        return BLE_ERROR_NOT_IMPLEMENTED;
    }

    _privacy_enabled = enable;

    update_address_resolution_setting();

    return BLE_ERROR_NONE;
}


ble_error_t Gap::setPeripheralPrivacyConfiguration(
    const peripheral_privacy_configuration_t *configuration
)
{
    _peripheral_privacy_configuration = *configuration;

    update_address_resolution_setting();

    return BLE_ERROR_NONE;
}


ble_error_t Gap::getPeripheralPrivacyConfiguration(
    peripheral_privacy_configuration_t *configuration
)
{
    *configuration = _peripheral_privacy_configuration;

    return BLE_ERROR_NONE;
}


ble_error_t Gap::setCentralPrivacyConfiguration(
    const central_privacy_configuration_t *configuration
)
{
    _central_privacy_configuration = *configuration;

    update_address_resolution_setting();

    return BLE_ERROR_NONE;
}


ble_error_t Gap::getCentralPrivacyConfiguration(
    central_privacy_configuration_t *configuration
)
{
    *configuration = _central_privacy_configuration;

    return BLE_ERROR_NONE;
}


ble_error_t Gap::reset()
{
    /* Notify that the instance is about to shut down */
//    shutdownCallChain.call(this);
    shutdownCallChain.clear();

    _event_handler = nullptr;

#if BLE_ROLE_BROADCASTER
    _advertising_timeout.detach();
#endif
#if BLE_ROLE_OBSERVER
    _scan_timeout.detach();
#endif

#if BLE_FEATURE_EXTENDED_ADVERTISING
    if (is_extended_advertising_available()) {
        /* stop all advertising sets */
        for (size_t i = 1; i < BLE_GAP_MAX_ADVERTISING_SETS; ++i) {
            if (_active_sets.get(i)) {
                _pal_gap.extended_advertising_enable(
                    /* enable */ false,
                    /* number of advertising sets */ 1,
                    (advertising_handle_t *) &i,
                    nullptr,
                    nullptr
                );
            }
#if BLE_FEATURE_PERIODIC_ADVERTISING
            if (_active_periodic_sets.get(i)) {
                _pal_gap.periodic_advertising_enable(
                    /* enable */ false,
                    (advertising_handle_t) i
                );
            }
#endif // BLE_FEATURE_PERIODIC_ADVERTISING
        }

        /* clear state of all advertising sets */
        _existing_sets.clear();
#endif // BLE_FEATURE_EXTENDED_ADVERTISING
#if BLE_FEATURE_PERIODIC_ADVERTISING
        _active_periodic_sets.clear();
#endif
        if (_active_sets.get(LEGACY_ADVERTISING_HANDLE)) {
            _pal_gap.extended_advertising_enable(
                /* enable */ false,
                /* number of advertising sets */ 1,
                (advertising_handle_t *) &LEGACY_ADVERTISING_HANDLE,
                nullptr,
                nullptr
            );
        }
        _active_sets.clear();
        _connectable_payload_size_exceeded.clear();
        _set_is_connectable.clear();

        /* clear advertising set data on the controller */
        _pal_gap.clear_advertising_sets();
#if BLE_FEATURE_EXTENDED_ADVERTISING
    }
#endif // BLE_FEATURE_EXTENDED_ADVERTISING

    return BLE_ERROR_NONE;
}

void Gap::onShutdown(const GapShutdownCallback_t &callback)
{
    shutdownCallChain.add(callback);
}

Gap::GapShutdownCallbackChain_t &Gap::onShutdown()
{
    return shutdownCallChain;
}


void Gap::on_scan_timeout()
{
    if (!_scan_enabled) {
        return;
    }

    _scan_enabled = false;

    if (!is_extended_advertising_available()) {
        /* if timeout happened on a 4.2 chip this means legacy scanning and a timer timeout
         * but we need to handle the event from user context - use the event queue to handle it */
        _event_queue.post(
            mbed::callback(
                this,
                &Gap::process_legacy_scan_timeout
            )
        );
    } else {
        if (_event_handler) {
            _event_handler->onScanTimeout(ScanTimeoutEvent());
        }
    }
}


void Gap::process_legacy_scan_timeout()
{
    /* legacy scanning timed out is based on timer so we need to stop the scan manually */
    _pal_gap.scan_enable(false, false);
#if BLE_FEATURE_PRIVACY
    set_random_address_rotation(false);
#endif

    if (_event_handler) {
        _event_handler->onScanTimeout(ScanTimeoutEvent());
    }
}


void Gap::on_advertising_timeout()
{
    _event_queue.post(mbed::callback(this, &Gap::process_advertising_timeout));
}


void Gap::process_advertising_timeout()
{
    // This will signal timeout via onAdvertisingEnd()
    ble_error_t err = _pal_gap.advertising_enable(false);
    if (err) {
        // TODO: define the mechanism signaling the error
    }

#if BLE_FEATURE_PRIVACY
    // Stop address rotation if required
    set_random_address_rotation(false);
#endif
}


void Gap::on_gap_event_received(const GapEvent &e)
{
    switch (e.type.value()) {
#if BLE_ROLE_OBSERVER
        case GapEventType::ADVERTISING_REPORT:
            on_advertising_report(static_cast<const GapAdvertisingReportEvent &>(e));
            break;
#endif // BLE_ROLE_OBSERVER
#if BLE_FEATURE_CONNECTABLE
        case GapEventType::CONNECTION_COMPLETE:
            on_connection_complete(static_cast<const GapConnectionCompleteEvent &>(e));
            break;

        case GapEventType::CONNECTION_UPDATE:
            on_connection_update(static_cast<const GapConnectionUpdateEvent &>(e));
            break;

        case GapEventType::DISCONNECTION_COMPLETE:
            on_disconnection_complete(static_cast<const GapDisconnectionCompleteEvent &>(e));
            break;

        case GapEventType::REMOTE_CONNECTION_PARAMETER_REQUEST:
            on_connection_parameter_request(static_cast<const GapRemoteConnectionParameterRequestEvent &>(e));
            break;
#endif // BLE_FEATURE_CONNECTABLE
        case GapEventType::UNEXPECTED_ERROR:
            on_unexpected_error(static_cast<const GapUnexpectedErrorEvent &>(e));
            break;

        default:
            break;
    }
}


void Gap::on_advertising_report(const GapAdvertisingReportEvent &e)
{
    for (size_t i = 0; i < e.size(); ++i) {
        GapAdvertisingReportEvent::advertising_t advertising = e[i];

#if BLE_FEATURE_PRIVACY
        // Check if the address hasn't been resolved
        if (_privacy_enabled &&
            _central_privacy_configuration.resolution_strategy == central_privacy_configuration_t::RESOLVE_AND_FILTER &&
            advertising.address_type == connection_peer_address_type_t::RANDOM_ADDRESS &&
            is_random_private_resolvable_address(advertising.address.data())
            ) {
            // Filter it out
            continue;
        }
#endif // BLE_FEATURE_PRIVACY

        // note 1-to-1 conversion between connection_peer_address_type_t and
        // peer_address_type_t
        peer_address_type_t peer_address_type =
            static_cast<peer_address_type_t::type>(advertising.address_type.value());

        // report in new event handler
        if (_event_handler) {
            uint8_t event_type = 0;

            // Conversion table available at BLUETOOTH SPECIFICATION Version 5.0 | Vol 2, Part E
            // 7.7.65.13
            switch (advertising.type.value()) {
                case received_advertising_type_t::ADV_IND:
                    event_type = 0x13;
                    break;
                case received_advertising_type_t::ADV_DIRECT_IND:
                    event_type = 0x15;
                    break;
                case received_advertising_type_t::ADV_SCAN_IND:
                    event_type = 0x12;
                    break;
                case received_advertising_type_t::ADV_NONCONN_IND:
                    event_type = 0x10;
                    break;
                case received_advertising_type_t::SCAN_RESPONSE:
                    event_type = 0x1B;
                    break;
            }

            _event_handler->onAdvertisingReport(
                AdvertisingReportEvent(
                    advertising_event_t(event_type),
                    peer_address_type,
                    advertising.address,
                    /* primary */ phy_t::LE_1M,
                    /* secondary */ phy_t::NONE,
                    /* SID - NO ADI FIELD IN THE PDU */ 0xFF,
                    /* tx power information not available */ 127,
                    advertising.rssi,
                    /* NO PERIODIC ADVERTISING */ 0,
                    peer_address_type_t::ANONYMOUS,
                    ble::address_t(),
                    Span<const uint8_t>(advertising.data.data(), advertising.data.size())
                )
            );
        }
    }
}


void Gap::on_connection_complete(const GapConnectionCompleteEvent &e)
{
    if (e.status != hci_error_code_t::SUCCESS) {
        if (_event_handler) {
            _event_handler->onConnectionComplete(
                ConnectionCompleteEvent(
                    BLE_ERROR_NOT_FOUND,
                    INVALID_ADVERTISING_HANDLE,
                    connection_role_t::CENTRAL,
                    peer_address_type_t::ANONYMOUS,
                    ble::address_t(),
                    ble::address_t(),
                    ble::address_t(),
                    ble::conn_interval_t::max(),
                    /* dummy slave latency */ 0,
                    ble::supervision_timeout_t::max(),
                    /* master clock accuracy */ 0
                )
            );
        }

        return;
    }

    bool needs_pairing = false;
    bool needs_authentication = false;

#if BLE_ROLE_PERIPHERAL
#if BLE_FEATURE_PRIVACY
    if (_privacy_enabled &&
        e.role.value() == e.role.PERIPHERAL &&
        e.peer_address_type == peer_address_type_t::RANDOM
        ) {
        // Apply privacy policy if in peripheral mode for non-resolved addresses
        ble::random_address_type_t random_address_type(ble::random_address_type_t::RESOLVABLE_PRIVATE);
        ble_error_t err = getRandomAddressType(e.peer_address.data(), &random_address_type);
        if (err) {
            // FIXME: return for now; needs to report the error ?
            return;
        }

        if (random_address_type == ble::random_address_type_t::RESOLVABLE_PRIVATE) {
            switch (_peripheral_privacy_configuration.resolution_strategy) {
                case peripheral_privacy_configuration_t::REJECT_NON_RESOLVED_ADDRESS:
                    // Reject connection request - the user will get notified through a callback
                    _pal_gap.disconnect(
                        e.connection_handle,
                        local_disconnection_reason_t::AUTHENTICATION_FAILURE
                    );
                    return;

                case peripheral_privacy_configuration_t::PERFORM_PAIRING_PROCEDURE:
                    needs_pairing = true;
                    break;

                case peripheral_privacy_configuration_t::PERFORM_AUTHENTICATION_PROCEDURE:
                    needs_authentication = true;
                    break;

                default:
                    break;
            }
        }
    }
#endif // BLE_FEATURE_PRIVACY

    if (e.role.value() == e.role.PERIPHERAL) {
        _advertising_timeout.detach();
        _pal_gap.advertising_enable(false);

#if BLE_FEATURE_PRIVACY
        // Stop address rotation if required
        set_random_address_rotation(false);
#endif
    }
#endif // BLE_ROLE_PERIPHERAL

    ble::address_t address;
    if (_address_type == own_address_type_t::PUBLIC) {
        address = _pal_gap.get_device_address();
    } else {
        address = _pal_gap.get_random_address();
    }

    // signal internal stack
    if (_connection_event_handler) {
        _connection_event_handler->on_connected(
            e.connection_handle,
            e.role,
            e.peer_address_type,
            e.peer_address,
            _address_type,
            address
        );
    }

    // signal application
    if (_event_handler) {
        _event_handler->onConnectionComplete(
            ConnectionCompleteEvent(
                BLE_ERROR_NONE,
                e.connection_handle,
                e.role,
                e.peer_address_type,
                e.peer_address,
                e.local_resolvable_private_address,
                e.peer_resolvable_private_address,
                conn_interval_t(e.connection_interval),
                e.connection_latency,
                supervision_timeout_t(e.supervision_timeout),
                /* default master clock accuracy */ ble::clock_accuracy_t::PPM_500
            )
        );
    }

#if BLE_FEATURE_SECURITY
    // Now starts pairing or authentication procedures if required
    if (needs_pairing) {
        ble::SecurityManager &sm = createBLEInstance()->getSecurityManager();
        // Request authentication to start pairing procedure
        sm.requestAuthentication(e.connection_handle);
    } else if (needs_authentication) {
        // TODO: GAP Authentication != Security Manager authentication
        // Needs to be implemented
    }
#endif // BLE_FEATURE_SECURITY
}


void Gap::on_disconnection_complete(const GapDisconnectionCompleteEvent &e)
{
    if (e.status == hci_error_code_t::SUCCESS) {
        // signal internal stack
        if (_connection_event_handler) {
            _connection_event_handler->on_disconnected(
                (connection_handle_t) e.connection_handle,
                disconnection_reason_t(e.reason)
            );
        }

        // signal application
        if (_event_handler) {
            _event_handler->onDisconnectionComplete(
                DisconnectionCompleteEvent(
                    (connection_handle_t) e.connection_handle,
                    (disconnection_reason_t::type) e.reason
                )
            );
        }
    } else {
        // TODO: define what to do in case of failure
    }
}


void Gap::on_connection_parameter_request(const GapRemoteConnectionParameterRequestEvent &e)
{
    if (_user_manage_connection_parameter_requests) {
        if (_event_handler) {
            _event_handler->onUpdateConnectionParametersRequest(
                UpdateConnectionParametersRequestEvent(
                    e.connection_handle,
                    conn_interval_t(e.min_connection_interval),
                    conn_interval_t(e.max_connection_interval),
                    e.connection_latency,
                    supervision_timeout_t(e.supervision_timeout)
                )
            );
        } else {
            MBED_ERROR(illegal_state_error, "Event handler required if connection params are user handled");
        }
    } else {
        _pal_gap.accept_connection_parameter_request(
            e.connection_handle,
            e.min_connection_interval,
            e.max_connection_interval,
            e.connection_latency,
            e.supervision_timeout,
            /* minimum_connection_event_length */ 0,
            /* maximum_connection_event_length */ 0
        );
    }
}


void Gap::on_connection_update(const GapConnectionUpdateEvent &e)
{
    if (!_event_handler) {
        return;
    }

    _event_handler->onConnectionParametersUpdateComplete(
        ConnectionParametersUpdateCompleteEvent(
            e.status == hci_error_code_t::SUCCESS ? BLE_ERROR_NONE : BLE_ERROR_UNSPECIFIED,
            e.connection_handle,
            conn_interval_t(e.connection_interval),
            e.connection_latency,
            supervision_timeout_t(e.supervision_timeout)
        )
    );
}


void Gap::on_unexpected_error(const GapUnexpectedErrorEvent &e)
{
    // TODO: add feature in interface to notify the user that the connection
    // has been updated.
}


own_address_type_t Gap::get_own_address_type(AddressUseType_t address_use_type)
{
#if BLE_FEATURE_PRIVACY
    if (_privacy_enabled) {
        bool use_non_resolvable_address = false;
#if BLE_ROLE_OBSERVER
        if (address_use_type == CENTRAL_SCAN) {
            use_non_resolvable_address = _central_privacy_configuration.use_non_resolvable_random_address;
        } else
#endif
#if BLE_ROLE_BROADCASTER
        if (address_use_type == PERIPHERAL_NON_CONNECTABLE) {
            use_non_resolvable_address = _peripheral_privacy_configuration.use_non_resolvable_random_address;
        }
#endif // BLE_ROLE_BROADCASTER

        // An non resolvable private address should be generated
        if (use_non_resolvable_address) {
            return own_address_type_t::RANDOM;
        }

        switch (_address_type.value()) {
            case own_address_type_t::PUBLIC:
                return own_address_type_t::RESOLVABLE_PRIVATE_ADDRESS_PUBLIC_FALLBACK;
            default:
                return own_address_type_t::RESOLVABLE_PRIVATE_ADDRESS_RANDOM_FALLBACK;
        }
    }
#endif // BLE_FEATURE_PRIVACY

    switch (_address_type.value()) {
        case own_address_type_t::PUBLIC:
            return own_address_type_t::PUBLIC;
        default:
            return own_address_type_t::RANDOM;
    }
}


bool Gap::initialize_whitelist() const
{
    if (_whitelist.addresses != nullptr) {
        return true;
    }

    uint8_t whitelist_capacity = _pal_gap.read_white_list_capacity();

    if (whitelist_capacity == 0) {
        return false;
    }

    _whitelist.addresses = new(std::nothrow) whitelist_t::entry_t[whitelist_capacity];
    if (_whitelist.addresses == nullptr) {
        return false;
    }

    _whitelist.size = 0;
    _whitelist.capacity = whitelist_capacity;

    return true;
}


ble_error_t Gap::update_address_resolution_setting()
{
    // enable if privacy is enabled and resolution is requested in either central or peripheral mode
    bool enable = false;

    if (_privacy_enabled) {
#if BLE_ROLE_BROADCASTER
        if (_peripheral_privacy_configuration.resolution_strategy !=
            peripheral_privacy_configuration_t::DO_NOT_RESOLVE) {
            enable = true;
        }
#endif // BLE_ROLE_BROADCASTER
#if BLE_ROLE_OBSERVER
        if (_central_privacy_configuration.resolution_strategy != central_privacy_configuration_t::DO_NOT_RESOLVE) {
            enable = true;
        }
#endif // BLE_ROLE_OBSERVER
    }

    return _pal_gap.set_address_resolution(enable);
}


void Gap::set_random_address_rotation(bool enable)
{
    if (enable == _random_address_rotating) {
        return;
    }

    _random_address_rotating = enable;

    if (enable) {
        // Set first address
        update_random_address();

        // Schedule rotations every 15 minutes as recomended by the spec
        _address_rotation_ticker.attach(
            mbed::callback(this, &Gap::on_address_rotation_timeout),
            15min
        );
    } else {
        // Stop ticker
        _address_rotation_ticker.detach();

        // Set static random identity address
        _pal_gap.set_random_address(
            _random_static_identity_address
        );
    }
}


void Gap::update_random_address()
{
    if (!_random_address_rotating) {
        // This event might have been queued before we disabled address rotation
        return;
    }
#if BLE_FEATURE_EXTENDED_ADVERTISING
    if (is_extended_advertising_available()) {
        for (uint8_t i = 0; i < BLE_GAP_MAX_ADVERTISING_SETS; ++i) {
            if (_existing_sets.get(i)) {
                ble::address_t address;

                if (!getUnresolvableRandomAddress(address)) {
                    return;
                }

                /* ignore the error, if it fails to cycle because it's connectable */
                _pal_gap.set_advertising_set_random_address(
                    (advertising_handle_t) i,
                    address
                );
            }
        }
    }
#endif // BLE_FEATURE_EXTENDED_ADVERTISING

    ble::address_t address;

    if (!getUnresolvableRandomAddress(address)) {
        return;
    }

    ble_error_t err = _pal_gap.set_random_address(
        address
    );

    if (err) {
        return;
    }

    _address_type = own_address_type_t::RANDOM;
    _address = address;
}


bool Gap::getUnresolvableRandomAddress(ble::address_t &address)
{
    do {
        byte_array_t<8> random_data;
#if BLE_FEATURE_SECURITY
        ble_error_t ret = _pal_sm.get_random_data(random_data);
#else
        ble_error_t ret = BLE_ERROR_NOT_IMPLEMENTED;
#endif // BLE_FEATURE_SECURITY
        if (ret != BLE_ERROR_NONE) {
            // Abort
            return false;
        }

        // Build a non-resolvable private address as specified in the Core 4.2 spec, Vol 6, Part B, 1.3.2.2
        // Mask out two MSbs
        random_data[5] &= 0x3F;

        // Copy to address - will copy first 6 bytes
        address = ble::address_t(random_data.data());

        if (!is_random_private_non_resolvable_address(address.data())) {
            // If address is invalid, which is unlikely (all 0s or all 1s), try again
            // If implementation is faulty, we'll get stuck here
            continue;
        }

        // Address is valid
        break;
    }
    while (true);

    return true;
}


void Gap::on_address_rotation_timeout()
{
    _event_queue.post(mbed::callback(this, &Gap::update_random_address));
}

uint8_t Gap::getMaxAdvertisingSetNumber()
{
#if BLE_FEATURE_EXTENDED_ADVERTISING
    if (is_extended_advertising_available()) {
        uint8_t set_number = _pal_gap.get_max_number_of_advertising_sets();
        return std::min((uint8_t) BLE_GAP_MAX_ADVERTISING_SETS, set_number);
    } else
#endif // BLE_FEATURE_EXTENDED_ADVERTISING
    {
        return 1;
    }
}


uint16_t Gap::getMaxAdvertisingDataLength()
{
    return _pal_gap.get_maximum_advertising_data_length();
}


uint16_t Gap::getMaxConnectableAdvertisingDataLength()
{
    return _pal_gap.get_maximum_connectable_advertising_data_length();
}


uint16_t Gap::getMaxActiveSetAdvertisingDataLength()
{
    return _pal_gap.get_maximum_hci_advertising_data_length();
}


ble_error_t Gap::createAdvertisingSet(
    advertising_handle_t *handle,
    const AdvertisingParameters &parameters
)
{
    if (is_extended_advertising_available() == false) {
        return BLE_ERROR_OPERATION_NOT_PERMITTED;
    }

    uint8_t new_handle = LEGACY_ADVERTISING_HANDLE + 1;
    uint8_t end = getMaxAdvertisingSetNumber();

    *handle = INVALID_ADVERTISING_HANDLE;

    for (; new_handle < end; ++new_handle) {
        if (!_existing_sets.get(new_handle)) {
            ble_error_t err = setExtendedAdvertisingParameters(
                new_handle,
                parameters
            );
            if (err) {
                return err;
            }

            _existing_sets.set(new_handle);
            *handle = new_handle;

            return BLE_ERROR_NONE;
        }
    }

    return BLE_ERROR_NO_MEM;
}


ble_error_t Gap::destroyAdvertisingSet(advertising_handle_t handle)
{
    if (is_extended_advertising_available() == false) {
        return BLE_ERROR_OPERATION_NOT_PERMITTED;
    }

    if (handle == LEGACY_ADVERTISING_HANDLE) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (handle >= getMaxAdvertisingSetNumber()) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (!_existing_sets.get(handle)) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (_active_sets.get(handle)) {
        return BLE_ERROR_OPERATION_NOT_PERMITTED;
    }
#if BLE_FEATURE_PERIODIC_ADVERTISING
    if (_active_periodic_sets.get(handle)) {
        return BLE_ERROR_OPERATION_NOT_PERMITTED;
    }
#endif // BLE_FEATURE_PERIODIC_ADVERTISING

    ble_error_t err = _pal_gap.remove_advertising_set(handle);
    if (err) {
        return err;
    }

    _connectable_payload_size_exceeded.clear(handle);
    _set_is_connectable.clear(handle);
    _existing_sets.clear(handle);
    return BLE_ERROR_NONE;
}


ble_error_t Gap::setAdvertisingParameters(
    advertising_handle_t handle,
    const AdvertisingParameters &params
)
{
#if BLE_FEATURE_EXTENDED_ADVERTISING
    if (handle >= getMaxAdvertisingSetNumber()) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (!_existing_sets.get(handle)) {
        if (handle == LEGACY_ADVERTISING_HANDLE) {
            prepare_legacy_advertising_set();
        } else {
            return BLE_ERROR_INVALID_PARAM;
        }
    }

    if (is_extended_advertising_available()) {
        return setExtendedAdvertisingParameters(handle, params);
    } else
#endif // BLE_FEATURE_EXTENDED_ADVERTISING
    {
        if (handle != LEGACY_ADVERTISING_HANDLE) {
            return BLE_ERROR_INVALID_PARAM;
        }

        advertising_channel_map_t channel_map(
            params.getChannel37(),
            params.getChannel38(),
            params.getChannel39()
        );

        return _pal_gap.set_advertising_parameters(
            params.getMinPrimaryInterval().value(),
            params.getMaxPrimaryInterval().value(),
            params.getType(),
            params.getOwnAddressType(),
            params.getPeerAddressType(),
            params.getPeerAddress(),
            channel_map,
            params.getFilter()
        );
    }
}


ble_error_t Gap::setExtendedAdvertisingParameters(
    advertising_handle_t handle,
    const AdvertisingParameters &params
)
{
    if (handle >= getMaxAdvertisingSetNumber()) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (_active_sets.get(handle)) {
        return BLE_ERROR_OPERATION_NOT_PERMITTED;
    }

    /* check for illegal parameter combination */
    if ((
        params.getType() == advertising_type_t::CONNECTABLE_UNDIRECTED ||
            params.getType() == advertising_type_t::CONNECTABLE_DIRECTED
    ) &&
        params.getUseLegacyPDU() == false) {
        /* these types can only be used with legacy PDUs */
        return BLE_ERROR_INVALID_PARAM;
    }

    advertising_event_properties_t event_properties(params.getType());
    event_properties.include_tx_power = params.getTxPowerInHeader();
    event_properties.omit_advertiser_address = params.getAnonymousAdvertising();
    event_properties.use_legacy_pdu = params.getUseLegacyPDU();

    advertising_channel_map_t channel_map(
        params.getChannel37(),
        params.getChannel38(),
        params.getChannel39()
    );

    ble_error_t err = _pal_gap.set_extended_advertising_parameters(
        handle,
        event_properties,
        params.getMinPrimaryInterval().value(),
        params.getMaxPrimaryInterval().value(),
        channel_map,
        params.getOwnAddressType(),
        params.getPeerAddressType(),
        params.getPeerAddress(),
        params.getFilter(),
        params.getTxPower(),
        params.getPrimaryPhy(),
        params.getSecondaryMaxSkip(),
        params.getSecondaryPhy(),
        /* SID */ (handle % 0x10),
        params.getScanRequestNotification()
    );

    if (err) {
        return err;
    }

    if (event_properties.connectable) {
        _set_is_connectable.set(handle);
    } else {
        _set_is_connectable.clear(handle);
    }

    return _pal_gap.set_advertising_set_random_address(
        handle,
        _random_static_identity_address
    );
}


ble_error_t Gap::setAdvertisingPayload(
    advertising_handle_t handle,
    Span<const uint8_t> payload
)
{
    return setAdvertisingData(
        handle,
        payload,
        /* minimise fragmentation */ true,
        /* scan response */ false
    );
}


ble_error_t Gap::setAdvertisingScanResponse(
    advertising_handle_t handle,
    Span<const uint8_t> response
)
{
    return setAdvertisingData(
        handle,
        response,
        /* minimise fragmentation */ true,
        /* scan response */ true
    );
}


ble_error_t Gap::setAdvertisingData(
    advertising_handle_t handle,
    Span<const uint8_t> payload,
    bool minimiseFragmentation,
    bool scan_response
)
{
    // type declarations
    typedef advertising_fragment_description_t op_t;
    typedef ble_error_t (PalGap::*legacy_set_data_fn_t)(
        uint8_t,
        const advertising_data_t &
    );
    typedef ble_error_t (PalGap::*set_data_fn_t)(
        advertising_handle_t advertising_handle,
        op_t operation,
        bool minimize_fragmentation,
        uint8_t scan_response_data_size,
        const uint8_t *scan_response_data
    );

#if BLE_FEATURE_EXTENDED_ADVERTISING
    if (handle >= getMaxAdvertisingSetNumber()) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (!_existing_sets.get(handle)) {
        if (handle == LEGACY_ADVERTISING_HANDLE) {
            prepare_legacy_advertising_set();
        } else {
            return BLE_ERROR_INVALID_PARAM;
        }
    }

    // handle special case of legacy advertising
    if (is_extended_advertising_available() == false) {
#endif // BLE_FEATURE_EXTENDED_ADVERTISING
        if (handle != LEGACY_ADVERTISING_HANDLE) {
            return BLE_ERROR_INVALID_PARAM;
        }

        if (payload.size() > LEGACY_ADVERTISING_MAX_SIZE) {
            return BLE_ERROR_INVALID_PARAM;
        }

        // select the pal function
        legacy_set_data_fn_t set_data = scan_response ?
            &PalGap::set_scan_response_data :
            &PalGap::set_advertising_data;

        // set the payload
        return (_pal_gap.*set_data)(
            payload.size(),
            advertising_data_t(payload.data(), payload.size())
        );
#if BLE_FEATURE_EXTENDED_ADVERTISING
    }

    if (payload.size() > getMaxAdvertisingDataLength()) {
        MBED_WARNING(MBED_ERROR_INVALID_SIZE, "Payload size exceeds getMaxAdvertisingDataLength().");
        return BLE_ERROR_INVALID_PARAM;
    }

    if (!_active_sets.get(handle) && payload.size() > this->getMaxActiveSetAdvertisingDataLength()) {
        MBED_WARNING(MBED_ERROR_INVALID_SIZE, "Payload size for active sets needs to fit in a single operation"
                                              " - not greater than getMaxActiveSetAdvertisingDataLength().");
        return BLE_ERROR_INVALID_PARAM;
    }

    if (!scan_response) {
        if (payload.size() > this->getMaxConnectableAdvertisingDataLength()) {
            if (_active_sets.get(handle) && _set_is_connectable.get(handle)) {
                MBED_WARNING(MBED_ERROR_INVALID_SIZE, "Payload size for connectable advertising"
                                                      " exceeds getMaxAdvertisingDataLength().");
                return BLE_ERROR_INVALID_PARAM;
            } else {
                _connectable_payload_size_exceeded.set(handle);
            }
        } else {
            _connectable_payload_size_exceeded.clear(handle);
        }
    }

    // select the pal function
    set_data_fn_t set_data = scan_response ?
        &PalGap::set_extended_scan_response_data :
        &PalGap::set_extended_advertising_data;

    const size_t hci_length = _pal_gap.get_maximum_hci_advertising_data_length();

    for (size_t i = 0, end = payload.size(); (i < end) || (i == 0 && end == 0); i += hci_length) {
        // select the operation based on the index
        op_t op(op_t::INTERMEDIATE_FRAGMENT);
        if (end < hci_length) {
            op = op_t::COMPLETE_FRAGMENT;
        } else if (i == 0) {
            op = op_t::FIRST_FRAGMENT;
        } else if ((end - i) <= hci_length) {
            op = op_t::LAST_FRAGMENT;
        }

        // extract the payload
        Span<const uint8_t> sub_payload = payload.subspan(
            i,
            std::min(hci_length, (end - i))
        );

        // set the payload
        ble_error_t err = (_pal_gap.*set_data)(
            handle,
            op,
            minimiseFragmentation,
            sub_payload.size(),
            sub_payload.data()
        );

        if (err) {
            return err;
        }
    }
    return BLE_ERROR_NONE;
#endif // BLE_FEATURE_EXTENDED_ADVERTISING
}


ble_error_t Gap::startAdvertising(
    advertising_handle_t handle,
    adv_duration_t maxDuration,
    uint8_t maxEvents
)
{
    ble_error_t error = BLE_ERROR_NONE;

#if BLE_FEATURE_EXTENDED_ADVERTISING
    if (handle >= getMaxAdvertisingSetNumber()) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (!_existing_sets.get(handle)) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (_connectable_payload_size_exceeded.get(handle) && _set_is_connectable.get(handle)) {
        MBED_WARNING(MBED_ERROR_INVALID_SIZE, "Payload size exceeds size allowed for connectable advertising.");
        return BLE_ERROR_INVALID_STATE;
    }

    if (is_extended_advertising_available()) {
        error = _pal_gap.extended_advertising_enable(
            /* enable */ true,
            /* number of advertising sets */ 1,
            &handle,
            maxDuration.storage(),
            &maxEvents
        );

        if (error) {
            return error;
        }
    } else
#endif // BLE_FEATURE_EXTENDED_ADVERTISING
    {
        if (handle != LEGACY_ADVERTISING_HANDLE) {
            return BLE_ERROR_INVALID_PARAM;
        }

        error = _pal_gap.advertising_enable(true);
        if (error) {
            return error;
        }

        _advertising_timeout.detach();
        if (maxDuration.value()) {
            _advertising_timeout.attach(
                mbed::callback(this, &Gap::on_advertising_timeout),
                maxDuration.valueChrono()
            );
        }
    }

    _active_sets.set(handle);

    return error;
}


ble_error_t Gap::stopAdvertising(advertising_handle_t handle)
{
    ble_error_t status;

#if BLE_FEATURE_EXTENDED_ADVERTISING
    if (handle >= getMaxAdvertisingSetNumber()) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (!_existing_sets.get(handle)) {
        return BLE_ERROR_INVALID_PARAM;
    }
#endif // BLE_FEATURE_EXTENDED_ADVERTISING

    if (!_active_sets.get(handle)) {
        return BLE_ERROR_INVALID_STATE;
    }

#if BLE_FEATURE_EXTENDED_ADVERTISING
    if (is_extended_advertising_available()) {
        status = _pal_gap.extended_advertising_enable(
            /*enable ? */ false,
            /* number of advertising sets */ 1,
            &handle,
            nullptr,
            nullptr
        );

        if (status) {
            return status;
        }
    } else
#endif // BLE_FEATURE_EXTENDED_ADVERTISING
    {
        if (handle != LEGACY_ADVERTISING_HANDLE) {
            return BLE_ERROR_INVALID_PARAM;
        }

        status = _pal_gap.advertising_enable(false);

        if (status) {
            return status;
        }

        _advertising_timeout.detach();
    }

    _active_sets.clear(handle);

    return status;
}


bool Gap::isAdvertisingActive(advertising_handle_t handle)
{
    if (handle >= getMaxAdvertisingSetNumber()) {
        return false;
    }

    return _active_sets.get(handle);
}


ble_error_t Gap::setPeriodicAdvertisingParameters(
    advertising_handle_t handle,
    periodic_interval_t periodicAdvertisingIntervalMin,
    periodic_interval_t periodicAdvertisingIntervalMax,
    bool advertiseTxPower
)
{
    if (periodicAdvertisingIntervalMin.value() > periodicAdvertisingIntervalMax.value()) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (handle == LEGACY_ADVERTISING_HANDLE) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (handle >= getMaxAdvertisingSetNumber()) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (!_existing_sets.get(handle)) {
        return BLE_ERROR_INVALID_PARAM;
    }

    return _pal_gap.set_periodic_advertising_parameters(
        handle,
        periodicAdvertisingIntervalMin.value(),
        periodicAdvertisingIntervalMax.value(),
        advertiseTxPower
    );
}


ble_error_t Gap::setPeriodicAdvertisingPayload(
    advertising_handle_t handle,
    Span<const uint8_t> payload
)
{
    if (handle == LEGACY_ADVERTISING_HANDLE) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (handle >= getMaxAdvertisingSetNumber()) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (!_existing_sets.get(handle)) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (payload.size() > getMaxAdvertisingDataLength()) {
        return BLE_ERROR_INVALID_PARAM;
    }

    typedef advertising_fragment_description_t op_t;

    const size_t hci_length = _pal_gap.get_maximum_hci_advertising_data_length();
    size_t i = 0;
    size_t end = payload.size();

    /* always do at least one iteration for empty payload */
    do {
        // select the operation based on the index
        op_t op(op_t::INTERMEDIATE_FRAGMENT);
        if (end < hci_length) {
            op = op_t::COMPLETE_FRAGMENT;
        } else if (i == 0) {
            op = op_t::FIRST_FRAGMENT;
        } else if ((end - i) <= hci_length) {
            op = op_t::LAST_FRAGMENT;
        }

        // extract the payload
        Span<const uint8_t> sub_payload = payload.subspan(
            i,
            std::min(hci_length, (end - i))
        );

        ble_error_t err = _pal_gap.set_periodic_advertising_data(
            handle,
            op,
            sub_payload.size(),
            sub_payload.data()
        );

        if (err) {
            return err;
        }

        i += hci_length;
    }
    while (i < end);

    return BLE_ERROR_NONE;
}


ble_error_t Gap::startPeriodicAdvertising(advertising_handle_t handle)
{
    if (handle == LEGACY_ADVERTISING_HANDLE) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (handle >= getMaxAdvertisingSetNumber()) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (!_existing_sets.get(handle)) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (_active_sets.get(handle) == false) {
        return BLE_ERROR_INVALID_STATE;
    }

    if (_active_periodic_sets.get(handle) == true) {
        return BLE_ERROR_INVALID_STATE;
    }

    ble_error_t err = _pal_gap.periodic_advertising_enable(true, handle);
    if (err) {
        return err;
    }

    _active_periodic_sets.set(handle);
    return BLE_ERROR_NONE;
}


ble_error_t Gap::stopPeriodicAdvertising(advertising_handle_t handle)
{
    if (handle == LEGACY_ADVERTISING_HANDLE) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (handle >= getMaxAdvertisingSetNumber()) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (!_existing_sets.get(handle)) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (_active_periodic_sets.get(handle) == false) {
        return BLE_ERROR_INVALID_STATE;
    }

    ble_error_t err = _pal_gap.periodic_advertising_enable(false, handle);
    if (err) {
        return err;
    }

    _active_periodic_sets.clear(handle);
    return BLE_ERROR_NONE;
}


bool Gap::isPeriodicAdvertisingActive(advertising_handle_t handle)
{
    if (handle >= getMaxAdvertisingSetNumber()) {
        return false;
    }

    return _active_periodic_sets.get(handle);
}


void Gap::on_enhanced_connection_complete(
    hci_error_code_t status,
    connection_handle_t connection_handle,
    connection_role_t own_role,
    connection_peer_address_type_t peer_address_type,
    const ble::address_t &peer_address,
    const ble::address_t &local_resolvable_private_address,
    const ble::address_t &peer_resolvable_private_address,
    uint16_t connection_interval,
    uint16_t connection_latency,
    uint16_t supervision_timeout,
    clock_accuracy_t master_clock_accuracy
)
{
    if (!_event_handler) {
        return;
    }

    _event_handler->onConnectionComplete(
        ConnectionCompleteEvent(
            (status == hci_error_code_t::SUCCESS) ? BLE_ERROR_NONE : BLE_ERROR_INTERNAL_STACK_FAILURE,
            (connection_handle_t) connection_handle,
            own_role,
            (peer_address_type_t::type) peer_address_type.value(),
            peer_address,
            local_resolvable_private_address,
            peer_resolvable_private_address,
            conn_interval_t(connection_interval),
            connection_latency,
            supervision_timeout_t(supervision_timeout),
            master_clock_accuracy.get_ppm()
        )
    );
}


void Gap::on_extended_advertising_report(
    advertising_event_t event_type,
    const connection_peer_address_type_t *address_type,
    const ble::address_t &address,
    phy_t primary_phy,
    const phy_t *secondary_phy,
    advertising_sid_t advertising_sid,
    advertising_power_t tx_power,
    rssi_t rssi,
    uint16_t periodic_advertising_interval,
    direct_address_type_t direct_address_type,
    const ble::address_t &direct_address,
    uint8_t data_length,
    const uint8_t *data
)
{
#if BLE_FEATURE_PRIVACY
    // Check if the address hasn't been resolved
    if (_privacy_enabled &&
        _central_privacy_configuration.resolution_strategy == central_privacy_configuration_t::RESOLVE_AND_FILTER &&
        address_type != nullptr &&
        *address_type == connection_peer_address_type_t::RANDOM_ADDRESS &&
        is_random_private_resolvable_address(address.data())
        ) {
        return;
    }
#endif // BLE_FEATURE_PRIVACY

    if (!_event_handler) {
        return;
    }

    _event_handler->onAdvertisingReport(
        AdvertisingReportEvent(
            event_type,
            address_type ?
                (peer_address_type_t::type) address_type->value() :
                peer_address_type_t::ANONYMOUS,
            (address_t &) address,
            primary_phy,
            secondary_phy ? *secondary_phy : phy_t::NONE,
            advertising_sid,
            tx_power,
            rssi,
            periodic_advertising_interval,
            (ble::peer_address_type_t::type) direct_address_type.value(),
            (address_t &) direct_address,
            make_Span(data, data_length)
        )
    );
}


void Gap::on_periodic_advertising_sync_established(
    hci_error_code_t error,
    sync_handle_t sync_handle,
    advertising_sid_t advertising_sid,
    connection_peer_address_type_t advertiser_address_type,
    const ble::address_t &advertiser_address,
    phy_t advertiser_phy,
    uint16_t periodic_advertising_interval,
    clock_accuracy_t clock_accuracy
)
{
    if (!_event_handler) {
        return;
    }

    _event_handler->onPeriodicAdvertisingSyncEstablished(
        PeriodicAdvertisingSyncEstablishedEvent(
            (error == hci_error_code_t::SUCCESS) ? BLE_ERROR_NONE : BLE_ERROR_INTERNAL_STACK_FAILURE,
            sync_handle,
            advertising_sid,
            static_cast<ble::peer_address_type_t::type>(advertiser_address_type.value()),
            advertiser_address,
            advertiser_phy,
            periodic_advertising_interval,
            clock_accuracy
        )
    );
}


void Gap::on_periodic_advertising_report(
    sync_handle_t sync_handle,
    advertising_power_t tx_power,
    rssi_t rssi,
    advertising_data_status_t data_status,
    uint8_t data_length,
    const uint8_t *data
)
{
    if (!_event_handler) {
        return;
    }

    _event_handler->onPeriodicAdvertisingReport(
        PeriodicAdvertisingReportEvent(
            sync_handle,
            tx_power,
            rssi,
            data_status,
            make_const_Span(data, data_length)
        )
    );
}


void Gap::on_periodic_advertising_sync_loss(sync_handle_t sync_handle)
{
    if (!_event_handler) {
        return;
    }

    _event_handler->onPeriodicAdvertisingSyncLoss(
        PeriodicAdvertisingSyncLoss(sync_handle)
    );
}


void Gap::on_advertising_set_terminated(
    hci_error_code_t status,
    advertising_handle_t advertising_handle,
    connection_handle_t connection_handle,
    uint8_t number_of_completed_extended_advertising_events
)
{
    _active_sets.clear(advertising_handle);

    if (!_event_handler) {
        return;
    }

    _event_handler->onAdvertisingEnd(
        AdvertisingEndEvent(
            advertising_handle,
            connection_handle,
            number_of_completed_extended_advertising_events,
            status == hci_error_code_t::SUCCESS
        )
    );
}


void Gap::on_scan_request_received(
    advertising_handle_t advertising_handle,
    connection_peer_address_type_t scanner_address_type,
    const ble::address_t &address
)
{
    if (!_event_handler) {
        return;
    }

    _event_handler->onScanRequestReceived(
        ScanRequestEvent(
            advertising_handle,
            (peer_address_type_t::type) scanner_address_type.value(),
            address
        )
    );
}


void Gap::on_connection_update_complete(
    hci_error_code_t status,
    connection_handle_t connection_handle,
    uint16_t connection_interval,
    uint16_t connection_latency,
    uint16_t supervision_timeout
)
{
    if (!_event_handler) {
        return;
    }

    _event_handler->onConnectionParametersUpdateComplete(
        ConnectionParametersUpdateCompleteEvent(
            status == hci_error_code_t::SUCCESS ? BLE_ERROR_NONE : BLE_ERROR_UNSPECIFIED,
            connection_handle,
            conn_interval_t(connection_interval),
            slave_latency_t(connection_latency),
            supervision_timeout_t(supervision_timeout)
        )
    );
}


void Gap::on_remote_connection_parameter(
    connection_handle_t connection_handle,
    uint16_t connection_interval_min,
    uint16_t connection_interval_max,
    uint16_t connection_latency,
    uint16_t supervision_timeout
)
{
    if (_user_manage_connection_parameter_requests) {
        if (_event_handler) {
            _event_handler->onUpdateConnectionParametersRequest(
                UpdateConnectionParametersRequestEvent(
                    connection_handle,
                    conn_interval_t(connection_interval_min),
                    conn_interval_t(connection_interval_max),
                    connection_latency,
                    supervision_timeout_t(supervision_timeout))
            );
        } else {
            MBED_ERROR(illegal_state_error, "Event handler required if connection params are user handled");
        }
    } else {
        _pal_gap.accept_connection_parameter_request(
            connection_handle,
            connection_interval_min, connection_interval_max,
            connection_latency, supervision_timeout,
            /* minimum_connection_event_length */0,
            /* maximum_connection_event_length */0
        );
    }
}


ble_error_t Gap::setScanParameters(const ScanParameters &params)
{
    if (is_extended_advertising_available()) {
        bool active_scanning[] = {
            params.get1mPhyConfiguration().isActiveScanningSet(),
            params.getCodedPhyConfiguration().isActiveScanningSet()
        };

        uint16_t scan_interval[] = {
            params.get1mPhyConfiguration().getInterval().value(),
            params.getCodedPhyConfiguration().getInterval().value()
        };

        uint16_t scan_window[] = {
            params.get1mPhyConfiguration().getWindow().value(),
            params.getCodedPhyConfiguration().getWindow().value()
        };

        return _pal_gap.set_extended_scan_parameters(
            params.getOwnAddressType(),
            params.getFilter(),
            params.getPhys(),
            active_scanning,
            scan_interval,
            scan_window
        );
    } else {
        if (params.getPhys().get_coded()) {
            return BLE_ERROR_INVALID_PARAM;
        }

        ScanParameters::phy_configuration_t legacy_configuration =
            params.get1mPhyConfiguration();

        return _pal_gap.set_scan_parameters(
            legacy_configuration.isActiveScanningSet(),
            legacy_configuration.getInterval().value(),
            legacy_configuration.getWindow().value(),
            params.getOwnAddressType(),
            params.getFilter()
        );
    }
}


ble_error_t Gap::startScan(
    scan_duration_t duration,
    duplicates_filter_t filtering,
    scan_period_t period
)
{
#if BLE_FEATURE_PRIVACY
    if (_privacy_enabled && _central_privacy_configuration.use_non_resolvable_random_address) {
        set_random_address_rotation(true);
    }
#endif // BLE_FEATURE_PRIVACY
#if BLE_FEATURE_EXTENDED_ADVERTISING
    if (is_extended_advertising_available()) {
        ble_error_t err = _pal_gap.extended_scan_enable(
            /* enable */true,
            filtering,
            duration.value(),
            period.value()
        );

        if (err) {
            return err;
        }
    } else
#endif // BLE_FEATURE_EXTENDED_ADVERTISING
    {
        if (period.value() != 0) {
            return BLE_ERROR_INVALID_PARAM;
        }

        ble_error_t err = _pal_gap.scan_enable(
            true,
            filtering == duplicates_filter_t::DISABLE ? false : true
        );

        if (err) {
            return err;
        }

        _scan_timeout.detach();
        if (duration.value()) {
            _scan_timeout.attach(
                mbed::callback(this, &Gap::on_scan_timeout),
                duration.valueChrono()
            );
        }
    }

    _scan_enabled = true;

    return BLE_ERROR_NONE;
}


ble_error_t Gap::createSync(
    peer_address_type_t peerAddressType,
    const ble::address_t &peerAddress,
    advertising_sid_t sid,
    slave_latency_t maxPacketSkip,
    sync_timeout_t timeout
)
{
    if (is_extended_advertising_available() == false) {
        return BLE_ERROR_NOT_IMPLEMENTED;
    }

    if (peerAddressType != peer_address_type_t::PUBLIC &&
        peerAddressType != peer_address_type_t::RANDOM
        ) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (sid > 0x0F) {
        return BLE_ERROR_INVALID_PARAM;
    }

    return _pal_gap.periodic_advertising_create_sync(
        /* use advertiser list */ false,
        sid,
        (peer_address_type_t::type) peerAddressType.value(),
        peerAddress,
        maxPacketSkip.value(),
        timeout.value()
    );
}


ble_error_t Gap::createSync(
    slave_latency_t maxPacketSkip,
    sync_timeout_t timeout
)
{
    if (is_extended_advertising_available() == false) {
        return BLE_ERROR_NOT_IMPLEMENTED;
    }

    return _pal_gap.periodic_advertising_create_sync(
        /* use advertiser list */ true,
        /* sid - not used */ 0x00,
        /* N/A */ peer_address_type_t::PUBLIC,
        /* N/A */ ble::address_t(),
        maxPacketSkip.value(),
        timeout.value()
    );
}


ble_error_t Gap::cancelCreateSync()
{
    if (is_extended_advertising_available() == false) {
        return BLE_ERROR_NOT_IMPLEMENTED;
    }

    return _pal_gap.cancel_periodic_advertising_create_sync();
}


ble_error_t Gap::terminateSync(periodic_sync_handle_t handle)
{
    if (is_extended_advertising_available() == false) {
        return BLE_ERROR_NOT_IMPLEMENTED;
    }

    return _pal_gap.periodic_advertising_terminate_sync(handle);
}


ble_error_t Gap::addDeviceToPeriodicAdvertiserList(
    peer_address_type_t peerAddressType,
    const ble::address_t &peerAddress,
    advertising_sid_t sid
)
{
    if (is_extended_advertising_available() == false) {
        return BLE_ERROR_NOT_IMPLEMENTED;
    }

    if (peerAddressType != peer_address_type_t::PUBLIC ||
        peerAddressType != peer_address_type_t::RANDOM
        ) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (sid > 0x0F) {
        return BLE_ERROR_INVALID_PARAM;
    }

    return _pal_gap.add_device_to_periodic_advertiser_list(
        (advertising_peer_address_type_t::type) peerAddressType.value(),
        peerAddress,
        sid
    );
}


ble_error_t Gap::removeDeviceFromPeriodicAdvertiserList(
    peer_address_type_t peerAddressType,
    const ble::address_t &peerAddress,
    advertising_sid_t sid
)
{
    if (is_extended_advertising_available() == false) {
        return BLE_ERROR_NOT_IMPLEMENTED;
    }

    if (peerAddressType != peer_address_type_t::PUBLIC ||
        peerAddressType != peer_address_type_t::RANDOM
        ) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (sid > 0x0F) {
        return BLE_ERROR_INVALID_PARAM;
    }

    return _pal_gap.remove_device_from_periodic_advertiser_list(
        (advertising_peer_address_type_t::type) peerAddressType.value(),
        peerAddress,
        sid
    );
}


ble_error_t Gap::clearPeriodicAdvertiserList()
{
    if (is_extended_advertising_available() == false) {
        return BLE_ERROR_NOT_IMPLEMENTED;
    }

    return _pal_gap.clear_periodic_advertiser_list();
}


uint8_t Gap::getMaxPeriodicAdvertiserListSize()
{
    if (is_extended_advertising_available() == false) {
        return BLE_ERROR_NOT_IMPLEMENTED;
    }

    return _pal_gap.read_periodic_advertiser_list_size();
}


bool Gap::is_extended_advertising_available()
{
#if BLE_FEATURE_EXTENDED_ADVERTISING
    return isFeatureSupported(
        controller_supported_features_t::LE_EXTENDED_ADVERTISING
    );
#else
    return false;
#endif // BLE_FEATURE_EXTENDED_ADVERTISING
}


void Gap::prepare_legacy_advertising_set()
{
    if (_existing_sets.get(LEGACY_ADVERTISING_HANDLE)) {
        return;
    }

    setExtendedAdvertisingParameters(
        LEGACY_ADVERTISING_HANDLE,
        AdvertisingParameters()
    );
    _existing_sets.set(LEGACY_ADVERTISING_HANDLE);
}

void Gap::setEventHandler(Gap::EventHandler *handler)
{
    _event_handler = handler;
}

} // impl
} // ble
