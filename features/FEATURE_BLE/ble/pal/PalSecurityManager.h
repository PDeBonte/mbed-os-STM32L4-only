/* mbed Microcontroller Library
 * Copyright (c) 2017-2018 ARM Limited
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

#ifndef MBED_OS_FEATURES_FEATURE_BLE_BLE_PAL_PALSM_H_
#define MBED_OS_FEATURES_FEATURE_BLE_BLE_PAL_PALSM_H_

#include "platform/Callback.h"
#include "platform/NonCopyable.h"
#include "ble/BLETypes.h"
#include "ble/SafeEnum.h"
#include "ble/BLEProtocol.h"
#include "ble/SecurityManager.h"
#include "ble/pal/GapTypes.h"

namespace ble {
namespace pal {

/**
 * Type that describe the IO capability of a device; it is used during Pairing
 * Feature exchange.
 */
struct io_capability_t : SafeEnum<io_capability_t, uint8_t> {
    enum type {
        DISPLAY_ONLY = 0x00,
        DISPLAY_YES_NO = 0x01,
        KEYBOARD_ONLY = 0x02,
        NO_INPUT_NO_OUTPUT = 0x03,
        KEYBOARD_DISPLAY = 0x04
    };

    /**
     * Construct a new instance of io_capability_t.
     */
    io_capability_t(type value) : SafeEnum<io_capability_t, uint8_t>(value) { }
};


/**
 * Type that describe a pairing failure.
 */
struct pairing_failure_t : SafeEnum<pairing_failure_t, uint8_t> {
    enum type {
        PASSKEY_ENTRY_FAILED = 0x01,
        OOB_NOT_AVAILABLE = 0x02,
        AUTHENTICATION_REQUIREMENTS = 0x03,
        CONFIRM_VALUE_FAILED = 0x04,
        PAIRING_NOT_SUPPORTED = 0x05,
        ENCRYPTION_KEY_SIZE = 0x06,
        COMMAND_NOT_SUPPORTED = 0x07,
        UNSPECIFIED_REASON = 0x08,
        REPEATED_ATTEMPTS = 0x09,
        INVALID_PARAMETERS = 0x0A,
        DHKEY_CHECK_FAILED = 0x0B,
        NUMERIC_COMPARISON_FAILED = 0x0c,
        BR_EDR_PAIRING_IN_PROGRESS = 0x0D,
        CROSS_TRANSPORT_KEY_DERIVATION_OR_GENERATION_NOT_ALLOWED = 0x0E
    };

    /**
     * Construct a new instance of pairing_failure_t.
     */
    pairing_failure_t(type value) : SafeEnum<pairing_failure_t, uint8_t>(value) { }
};


using SecurityManager::IO_CAPS_NONE;
using SecurityManager::SecurityCompletionStatus_t;
using SecurityManager::SecurityMode_t;
using SecurityManager::LinkSecurityStatus_t;
using SecurityManager::Keypress_t;

/* please use typedef for porting not the types directly */

typedef uint8_t passkey_t[6];
typedef uint8_t oob_data_t[16];

typedef uint8_t irk_t[16];
typedef uint8_t csrk_t[16];
typedef uint8_t ltk_t[16];
typedef uint8_t ediv_t[8];
typedef uint8_t rand_t[2];
typedef uint32_t passkey_num_t;

typedef uint8_t key_distribution_t;

enum KeyDistributionFlags_t {
    KEY_DISTRIBUTION_NONE       = 0x00,
    KEY_DISTRIBUTION_ENCRYPTION = 0x01,
    KEY_DISTRIBUTION_IDENTITY   = 0x02,
    KEY_DISTRIBUTION_SIGNING    = 0x04,
    KEY_DISTRIBUTION_LINK       = 0x08,
    KEY_DISTRIBUTION_ALL        = 0x0F
};

typedef uint8_t authentication_t;

enum AuthenticationFlags_t {
    AUTHENTICATION_BONDING                = 0x01,
    AUTHENTICATION_MITM                   = 0x04, /* 0x02 missing because bonding uses two bits */
    AUTHENTICATION_SECURE_CONNECTIONS     = 0x08,
    AUTHENTICATION_KEYPRESS_NOTIFICATION  = 0x10
};

/**
 * Handle events generated by ble::pal::SecurityManager
 */
class SecurityManagerEventHandler {
public:
    /**
     * Called upon reception of a pairing request.
     *
     * Upper layer shall either send a pairing response (send_pairing_response)
     * or cancel the pairing procedure (cancel_pairing).
     */
    virtual void on_pairing_request(
        connection_handle_t connection,
        io_capability_t io_capability,
        bool oob_data_flag,
        authentication_t authentication_requirements,
        uint8_t maximum_encryption_key_size,
        key_distribution_t initiator_dist,
        key_distribution_t responder_dist
    ) = 0;

    /**
     * Called upon reception of Pairing failed.
     *
     * @note Any subsequent pairing procedure shall restart from the Pairing
     * Feature Exchange phase.
     */
    virtual void on_pairing_error(
        connection_handle_t connection,
        pairing_failure_t error
    ) = 0;

    /**
     * Called when the application should display a passkey.
     */
    virtual void on_passkey_display(
        connection_handle_t handle, const passkey_t& passkey
    ) = 0;

    /**
     * Request the passkey entered during pairing.
     *
     * @note shall be followed by: pal::SecurityManager::passkey_request_reply
     * or a cancellation of the procedure.
     */
    virtual void on_passkey_request(connection_handle_t handle) = 0;

    /**
     * Request oob data entered during pairing
     *
     * @note shall be followed by: pal::SecurityManager::oob_data_request_reply
     * or a cancellation of the procedure.
     */
    virtual void on_oob_data_request(connection_handle_t handle) = 0;


    virtual void security_setup_initiated(
        connection_handle_t handle,
        bool allow_bonding,
        bool require_mitm,
        io_capability_t iocaps
    ) = 0;

    virtual void security_setup_completed(
        connection_handle_t handle,
        SecurityManager::SecurityCompletionStatus_t status
    ) = 0;

    virtual void link_secured(
        connection_handle_t handle, SecurityManager::SecurityMode_t security_mode
    ) = 0;


    virtual void valid_mic_timeout(connection_handle_t handle) = 0;

    virtual void link_key_failure(connection_handle_t handle) = 0;

    virtual void keypress_notification(connection_handle_t handle, SecurityManager::Keypress_t keypress) = 0;

    virtual void legacy_pariring_oob_request(connection_handle_t handle) = 0;

    virtual void confirmation_request(connection_handle_t handle) = 0;

    virtual void keys_exchanged(
        connection_handle_t handle,
        advertising_peer_address_type_t peer_identity_address_type,
        address_t &peer_identity_address,
        ediv_t &ediv,
        rand_t &rand,
        ltk_t &ltk,
        irk_t &irk,
        csrk_t &csrk
    ) = 0;

    virtual void ltk_request(
        connection_handle_t handle,
        ediv_t &ediv,
        rand_t &rand
    ) = 0;
};

/**
 * Adaptation layer of the Security Manager.
 */
class SecurityManager : private mbed::NonCopyable<SecurityManager> {
public:
    SecurityManager() : _pal_event_handler(NULL) { };

    virtual ~SecurityManager() { };

    ////////////////////////////////////////////////////////////////////////////
    // SM lifecycle management
    //

    virtual ble_error_t initialize() = 0;

    virtual ble_error_t terminate() = 0;

    virtual ble_error_t reset()  = 0;

    ////////////////////////////////////////////////////////////////////////////
    // Resolving list management
    //

    /**
     * Return the number of address translation entries that can be stored by the
     * subsystem.
     *
     * @warning: The number of entries is considered fixed.
     *
     * see BLUETOOTH SPECIFICATION Version 5.0 | Vol 2, Part E: 7.8.41
     */
    virtual uint8_t read_resolving_list_capacity() = 0;

    /**
     * Add a device definition into the resolving list of the LE subsystem.
     *
     * @see BLUETOOTH SPECIFICATION Version 5.0 | Vol 2, Part E: 7.8.38
     */
    virtual ble_error_t add_device_to_resolving_list(
        advertising_peer_address_type_t peer_identity_address_type,
        address_t peer_identity_address,
        irk_t peer_irk,
        irk_t local_irk
    ) = 0;


    /**
     * Add a device definition from the resolving list of the LE subsystem.
     *
     * @see BLUETOOTH SPECIFICATION Version 5.0 | Vol 2, Part E: 7.8.39
     */
    virtual ble_error_t remove_device_from_resolving_list(
        advertising_peer_address_type_t peer_identity_address_type,
        address_t peer_identity_address
    ) = 0;

    /**
     * Remove all devices from the resolving list.
     *
     * @see BLUETOOTH SPECIFICATION Version 5.0 | Vol 2, Part E: 7.8.40
     */
    virtual ble_error_t clear_resolving_list() = 0;

    ////////////////////////////////////////////////////////////////////////////
    // Feature support
    //

    virtual ble_error_t set_secure_connections_support(
        bool enabled, bool secure_connections_only = false
    ) = 0;

    virtual ble_error_t get_secure_connections_support(
        bool &enabled, bool &secure_connections_only
    ) = 0;

    ////////////////////////////////////////////////////////////////////////////
    // Security settings
    //
    virtual ble_error_t set_authentication_timeout(
        connection_handle_t, uint16_t timeout_in_10ms
    ) = 0;

    virtual ble_error_t get_authentication_timeout(
        connection_handle_t, uint16_t &timeout_in_10ms
    ) = 0;

    ////////////////////////////////////////////////////////////////////////////
    // Encryption
    //

    virtual ble_error_t enable_encryption(connection_handle_t handle) = 0;

    virtual ble_error_t disable_encryption(connection_handle_t handle) = 0;

    virtual ble_error_t get_encryption_status(
        connection_handle_t handle, LinkSecurityStatus_t &status
    ) = 0;

    virtual ble_error_t get_encryption_key_size(
        connection_handle_t, uint8_t &bitsize
    ) = 0;

    virtual ble_error_t refresh_encryption_key(connection_handle_t handle) = 0;

    ////////////////////////////////////////////////////////////////////////////
    // Privacy
    //

    virtual ble_error_t set_private_address_timeout(uint16_t timeout_in_seconds) = 0;

    ////////////////////////////////////////////////////////////////////////////
    // Keys
    //

    virtual ble_error_t set_ltk(connection_handle_t handle, ltk_t ltk) = 0;

    /**
     * Set the local IRK
     */
    virtual ble_error_t set_irk(const irk_t& irk) = 0;

    /**
     * Set the local csrk
     */
    virtual ble_error_t set_csrk(const csrk_t& csrk) = 0;

    virtual ble_error_t generate_irk() = 0;

    virtual ble_error_t generate_csrk() = 0;

    ////////////////////////////////////////////////////////////////////////////
    // Authentication
    //

    /**
     * Send a pairing request to a slave.
     *
     * @see BLUETOOTH SPECIFICATION Version 5.0 | Vol 3, Part H - 3.5.1
     */
    virtual ble_error_t send_pairing_request(
        connection_handle_t connection,
        io_capability_t io_capability,
        bool oob_data_flag,
        authentication_t authentication_requirements,
        uint8_t maximum_encryption_key_size,
        key_distribution_t initiator_dist,
        key_distribution_t responder_dist
    );

    /**
     * Send a pairing response to a master.
     *
     * @see BLUETOOTH SPECIFICATION Version 5.0 | Vol 3, Part H - 3.5.2
     */
    virtual ble_error_t send_pairing_response(
        connection_handle_t connection,
        io_capability_t io_capability,
        bool oob_data_flag,
        authentication_t authentication_requirements,
        uint8_t maximum_encryption_key_size,
        key_distribution_t initiator_dist,
        key_distribution_t responder_dist
    ) = 0;

    /**
     * Cancel an ongoing pairing
     *
     * @see BLUETOOTH SPECIFICATION Version 5.0 | Vol 3, Part H - 3.5.5
     */
    virtual ble_error_t cancel_pairing(
        connection_handle_t handle, pairing_failed_reason_t reason
    ) = 0;

    virtual ble_error_t request_authentication(connection_handle_t handle) = 0;

    ////////////////////////////////////////////////////////////////////////////
    // MITM
    //

    /**
     * Reply to a passkey request received from the SecurityManagerEventHandler.
     */
    virtual ble_error_t passkey_request_reply(
        connection_handle_t handle, const passkey_t& passkey
    ) = 0;

    /**
     * Reply to an oob data request received from the SecurityManagerEventHandler.
     */
    virtual ble_error_t oob_data_request_reply(
        connection_handle_t handle, const oob_data_t& oob_data
    ) = 0;


    virtual ble_error_t confirmation_entered(
        connection_handle_t handle, bool confirmation
    ) = 0;

    virtual ble_error_t send_keypress_notification(
        connection_handle_t handle, Keypress_t keypress
    ) = 0;

    /* Entry points for the underlying stack to report events back to the user. */
public:
    void set_event_handler(SecurityManagerEventHandler *event_handler) {
        _pal_event_handler = event_handler;
    }


protected:
    SecurityManagerEventHandler* get_event_handler() {
        return _pal_event_handler;
    }

private:
    SecurityManagerEventHandler *_pal_event_handler;

};

} /* namespace pal */
} /* namespace ble */

#endif /* MBED_OS_FEATURES_FEATURE_BLE_BLE_PAL_PALSM_H_ */
