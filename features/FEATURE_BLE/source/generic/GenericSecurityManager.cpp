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

#include "ble/SecurityManager.h"
#include "ble/pal/PalSecurityManager.h"
#include "ble/generic/GenericSecurityManager.h"

using ble::pal::advertising_peer_address_type_t;
using ble::pal::AuthenticationMask;
using ble::pal::KeyDistribution;
using ble::pal::connection_peer_address_type_t;

namespace ble {
namespace generic {

/* Implements SecurityManager */

////////////////////////////////////////////////////////////////////////////
// SM lifecycle management
//

ble_error_t GenericSecurityManager::init(
    bool bondable,
    bool mitm,
    SecurityIOCapabilities_t iocaps,
    const Passkey_t passkey,
    bool signing
) {
    ble_error_t err = _pal.initialize();
    if (err) {
    	return err;
    }

    _db.restore();
    _pal.set_io_capability((io_capability_t::type) iocaps);

    if (passkey) {
        _pal.set_display_passkey(PasskeyAscii::to_num(passkey));
    } else {
        _pal.set_display_passkey(0);
    }

    _legacy_pairing_allowed = true;

    bool secure_connections;
    _pal.get_secure_connections_support(secure_connections);

    _default_authentication.set_bondable(bondable);
    _default_authentication.set_mitm(mitm);
    _default_authentication.set_secure_connections(secure_connections);
    _default_authentication.set_keypress_notification(true);

    // FIXME: depends on BR/EDR support
    _default_key_distribution.set_link(false);

    _default_key_distribution.set_signing(signing);
    if (signing) {
        init_signing();
    }

    _connection_monitor.set_connection_event_handler(this);
    _signing_monitor.set_signing_event_handler(this);
    _pal.set_event_handler(this);

    uint8_t resolving_list_capacity = _pal.read_resolving_list_capacity();
    pal::SecurityEntryIdentity_t** identity_list_p =
        new (std::nothrow) pal::SecurityEntryIdentity_t*[resolving_list_capacity];

    if (identity_list_p) {
        ArrayView<pal::SecurityEntryIdentity_t*> identity_list(
            identity_list_p,
            resolving_list_capacity
        );

        _db.get_identity_list(
            mbed::callback(this, &GenericSecurityManager::on_identity_list_retrieved),
            identity_list
        );
    }

    return BLE_ERROR_NONE;
}

ble_error_t GenericSecurityManager::reset(void) {
    _db.sync();
    _pal.reset();
    SecurityManager::reset();

    return BLE_ERROR_NONE;
}

ble_error_t GenericSecurityManager::preserveBondingStateOnReset(bool enabled) {
    _db.set_restore(enabled);
    return BLE_ERROR_NONE;
}

////////////////////////////////////////////////////////////////////////////
// List management
//

ble_error_t GenericSecurityManager::purgeAllBondingState(void) {
    _db.clear_entries();
    return BLE_ERROR_NONE;
}

ble_error_t GenericSecurityManager::generateWhitelistFromBondTable(Gap::Whitelist_t *whitelist) const {
    if (eventHandler) {
        _db.generate_whitelist_from_bond_table(
            mbed::callback(eventHandler, &::SecurityManager::EventHandler::whitelistFromBondTable),
            whitelist
        );
    }
    return BLE_ERROR_NONE;
}

////////////////////////////////////////////////////////////////////////////
// Pairing
//

ble_error_t GenericSecurityManager::requestPairing(connection_handle_t connection) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (!_legacy_pairing_allowed && !_default_authentication.get_secure_connections()) {
        return BLE_ERROR_INVALID_STATE;
    }

    set_mitm_performed(connection, false);
    update_oob_presence(connection);

    AuthenticationMask link_authentication(_default_authentication);

    if (cb->mitm_requested) {
        link_authentication.set_mitm(true);
    }

    /* by default the initiator doesn't send any keys other then identity */
    KeyDistribution initiator_distribution(
        KeyDistribution::KEY_DISTRIBUTION_IDENTITY |
        _default_key_distribution.get_link()
    );

    initiator_distribution.set_signing(
        cb->signing_override_default ?
            cb->signing_requested :
            _default_key_distribution.get_signing()
    );

    /* if requested the initiator may send all the default keys for later
     * use when roles are changed */
    if (_master_sends_keys) {
        initiator_distribution = _default_key_distribution;
    }

    KeyDistribution responder_distribution(_default_key_distribution);

    if (cb->signing_override_default) {
        responder_distribution.set_signing(cb->signing_requested);
    }

    return _pal.send_pairing_request(
        connection,
        cb->oob_present,
        link_authentication,
        initiator_distribution,
        responder_distribution
    );
}

ble_error_t GenericSecurityManager::acceptPairingRequest(connection_handle_t connection) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return BLE_ERROR_INVALID_PARAM;
    }

    update_oob_presence(connection);

    AuthenticationMask link_authentication(_default_authentication);
    if (cb->mitm_requested) {
        link_authentication.set_mitm(true);
    }

    KeyDistribution initiator_distribution = cb->get_initiator_key_distribution();

    bool master_signing = initiator_distribution.get_signing();

    if (_master_sends_keys) {
        initiator_distribution &= _default_key_distribution;
    } else {
        initiator_distribution &= KeyDistribution(
            KeyDistribution::KEY_DISTRIBUTION_IDENTITY |
            KeyDistribution::KEY_DISTRIBUTION_LINK
        );
    }

    /* signing has to be offered and enabled on the link */
    if (master_signing) {
        initiator_distribution.set_signing(
            cb->signing_override_default ?
                cb->signing_requested :
                _default_key_distribution.get_signing()
        );
    }

    KeyDistribution responder_distribution(cb->get_responder_key_distribution());

    responder_distribution &= _default_key_distribution;

    /* signing has to be requested and enabled on the link */
    if (responder_distribution.get_signing()) {
        responder_distribution.set_signing(
            cb->signing_override_default ?
                cb->signing_requested :
                _default_key_distribution.get_signing()
        );
    }

    return _pal.send_pairing_response(
        connection,
        cb->oob_present,
        link_authentication,
        initiator_distribution,
        responder_distribution
    );
}

ble_error_t GenericSecurityManager::cancelPairingRequest(connection_handle_t connection) {
    return _pal.cancel_pairing(connection, pairing_failure_t::UNSPECIFIED_REASON);
}

ble_error_t GenericSecurityManager::setPairingRequestAuthorisation(bool required) {
    _pairing_authorisation_required = required;
    return BLE_ERROR_NONE;
}

////////////////////////////////////////////////////////////////////////////
// Feature support
//

ble_error_t GenericSecurityManager::allowLegacyPairing(bool allow) {
    _legacy_pairing_allowed = allow;
    return BLE_ERROR_NONE;
}

ble_error_t GenericSecurityManager::getSecureConnectionsSupport(bool *enabled) {
    return _pal.get_secure_connections_support(*enabled);
}

////////////////////////////////////////////////////////////////////////////
// Security settings
//

ble_error_t GenericSecurityManager::setIoCapability(SecurityIOCapabilities_t iocaps) {
    return _pal.set_io_capability((io_capability_t::type) iocaps);
}

ble_error_t GenericSecurityManager::setDisplayPasskey(const Passkey_t passkey) {
    return _pal.set_display_passkey(PasskeyAscii::to_num(passkey));
}

ble_error_t GenericSecurityManager::setAuthenticationTimeout(
    connection_handle_t connection,
    uint32_t timeout_in_ms
) {
    return _pal.set_authentication_timeout(connection, timeout_in_ms / 10);
}

ble_error_t GenericSecurityManager::getAuthenticationTimeout(
    connection_handle_t connection,
    uint32_t *timeout_in_ms
) {
    uint16_t timeout_in_10ms;
    ble_error_t status = _pal.get_authentication_timeout(connection, timeout_in_10ms);
    *timeout_in_ms = 10 * timeout_in_10ms;
    return status;
}

ble_error_t GenericSecurityManager::setLinkSecurity(
    connection_handle_t connection,
    SecurityMode_t securityMode
) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (cb->encryption_requested) {
        return BLE_ERROR_OPERATION_NOT_PERMITTED;
    }

    switch (securityMode) {
        case SECURITY_MODE_ENCRYPTION_OPEN_LINK:
            return setLinkEncryption(connection, link_encryption_t::NOT_ENCRYPTED);

        case SECURITY_MODE_ENCRYPTION_NO_MITM:
            return setLinkEncryption(connection, link_encryption_t::ENCRYPTED);

        case SECURITY_MODE_ENCRYPTION_WITH_MITM:
            return setLinkEncryption(connection, link_encryption_t::ENCRYPTED_WITH_MITM);

        case SECURITY_MODE_SIGNED_NO_MITM:
            return getSigningKey(connection, false);

        case SECURITY_MODE_SIGNED_WITH_MITM:
            return getSigningKey(connection, true);

        default:
            return BLE_ERROR_INVALID_PARAM;
    }
}

ble_error_t GenericSecurityManager::setKeypressNotification(bool enabled) {
    _default_authentication.set_keypress_notification(enabled);
    return BLE_ERROR_NONE;
}

ble_error_t GenericSecurityManager::enableSigning(
    connection_handle_t connection,
    bool enabled
) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return BLE_ERROR_INVALID_PARAM;
    }

    cb->signing_override_default = true;

    if (enabled && !cb->signing_requested && !_default_key_distribution.get_signing()) {
        cb->signing_requested = true;
        if (cb->csrk_stored) {
            /* used the stored ones when available */
            _db.get_entry_peer_csrk(
                mbed::callback(this, &GenericSecurityManager::set_peer_csrk_cb),
                cb->db_entry
            );
        } else {
            /* create keys if needed and exchange them */
            init_signing();
            if (cb->is_master) {
               return requestPairing(connection);
            } else {
               return slave_security_request(connection);
            }
        }
    } else {
        cb->signing_requested = enabled;
    }

    return BLE_ERROR_NONE;
}

ble_error_t GenericSecurityManager::setHintFutureRoleReversal(bool enable) {
    _master_sends_keys = enable;
    return BLE_ERROR_NONE;
}

////////////////////////////////////////////////////////////////////////////
// Encryption
//

ble_error_t GenericSecurityManager::getLinkEncryption(
    connection_handle_t connection,
    link_encryption_t *encryption
) {

    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (cb->encrypted) {
        if (cb->ltk_mitm_protected  || cb->mitm_performed) {
            if (cb->secure_connections_paired) {
                *encryption = link_encryption_t::ENCRYPTED_WITH_SC_AND_MITM;
            } else {
                *encryption = link_encryption_t::ENCRYPTED_WITH_MITM;
            }
        } else {
            *encryption = link_encryption_t::ENCRYPTED;
        }
    } else if (cb->encryption_requested) {
        *encryption = link_encryption_t::ENCRYPTION_IN_PROGRESS;
    } else {
        *encryption = link_encryption_t::NOT_ENCRYPTED;
    }

    return BLE_ERROR_NONE;
}

ble_error_t GenericSecurityManager::setLinkEncryption(
    connection_handle_t connection,
    link_encryption_t encryption
) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return BLE_ERROR_INVALID_PARAM;
    }

    link_encryption_t current_encryption(link_encryption_t::NOT_ENCRYPTED);
    getLinkEncryption(connection, &current_encryption);

    if (current_encryption == link_encryption_t::ENCRYPTION_IN_PROGRESS) {
        return BLE_ERROR_OPERATION_NOT_PERMITTED;
    }

    if (current_encryption == encryption) {

        /* ignore if the link is already at required state*/

    } else if (encryption == link_encryption_t::NOT_ENCRYPTED) {
        // Fail as it is not permitted to turn down encryption
        return BLE_ERROR_OPERATION_NOT_PERMITTED;
    } else if (encryption == link_encryption_t::ENCRYPTED) {

        /* only change if we're not already encrypted with mitm */
        if (current_encryption != link_encryption_t::ENCRYPTED_WITH_MITM ||
            current_encryption != link_encryption_t::ENCRYPTED_WITH_SC_AND_MITM
        ) {
            cb->encryption_requested = true;
            return enable_encryption(connection);
        }

    } else if (encryption == link_encryption_t::ENCRYPTED_WITH_MITM) {

        if (cb->ltk_mitm_protected && !cb->encrypted) {
            cb->encryption_requested = true;
            return enable_encryption(connection);
        } else {
            cb->encryption_requested = true;
            return requestAuthentication(connection);
        }

    } else if (encryption == link_encryption_t::ENCRYPTED_WITH_SC_AND_MITM) {

        if (cb->ltk_mitm_protected &&
            cb->secure_connections_paired &&
            !cb->encrypted
        ) {
            cb->encryption_requested = true;
            return enable_encryption(connection);
        } else {
            cb->encryption_requested = true;
            return requestAuthentication(connection);
        }

    } else {
        return BLE_ERROR_INVALID_PARAM;
    }

    eventHandler->linkEncryptionResult(connection, current_encryption);

    return BLE_ERROR_NONE;
}

ble_error_t GenericSecurityManager::getEncryptionKeySize(
    connection_handle_t connection,
    uint8_t *size
) {
    ControlBlock_t *cb = get_control_block(connection);
    if (cb) {
        *size = cb->encryption_key_size;
        return BLE_ERROR_NONE;
    } else {
        return BLE_ERROR_INVALID_PARAM;
    }
}

ble_error_t GenericSecurityManager::setEncryptionKeyRequirements(
    uint8_t minimumByteSize,
    uint8_t maximumByteSize
) {
    return _pal.set_encryption_key_requirements(minimumByteSize, maximumByteSize);
}

////////////////////////////////////////////////////////////////////////////
// Keys
//

ble_error_t GenericSecurityManager::getSigningKey(connection_handle_t connection, bool authenticated) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (cb->csrk_stored && (cb->csrk_mitm_protected || !authenticated)) {
        /* we have a key that is either authenticated or we don't care if it is
         * so retrieve it from the db now */
        _db.get_entry_peer_csrk(
            mbed::callback(this, &GenericSecurityManager::return_csrk_cb),
            cb->db_entry
        );
        return BLE_ERROR_NONE;

    } else {
        /* we don't have the right key so we need to get it first
         * keys exchange will create the signingKey event */
        if (authenticated) {
            return requestAuthentication(connection);
        } else if (cb->is_master) {
            return requestPairing(connection);
        } else {
            return slave_security_request(connection);
        }
    }
}

////////////////////////////////////////////////////////////////////////////
// Privacy
//

ble_error_t GenericSecurityManager::setPrivateAddressTimeout(uint16_t timeout_in_seconds) {
   return _pal.set_private_address_timeout(timeout_in_seconds);
}

////////////////////////////////////////////////////////////////////////////
// Authentication
//

ble_error_t GenericSecurityManager::requestAuthentication(connection_handle_t connection) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return BLE_ERROR_INVALID_PARAM;
    }

    if (cb->ltk_mitm_protected) {
        if (cb->authenticated) {
            return BLE_ERROR_NONE;
        } else {
            cb->encryption_requested = true;
            return enable_encryption(connection);
        }
    } else {
        cb->mitm_requested = true;
        if (cb->is_master) {
            return requestPairing(connection);
        } else {
            return slave_security_request(connection);
        }
    }
}

////////////////////////////////////////////////////////////////////////////
// MITM
//

ble_error_t GenericSecurityManager::generateOOB(
    const address_t *address
) {
    /* legacy pairing */
    ble_error_t status = get_random_data(_oob_temporary_key.data(), 16);

    if (status == BLE_ERROR_NONE) {
        _oob_temporary_key_creator_address = *address;

        eventHandler->legacyPairingOobGenerated(
            &_oob_temporary_key_creator_address,
            &_oob_temporary_key
        );
    } else {
        return status;
    }

    /* Secure connections. Avoid generating if we're already waiting for it.
     * If a local random is set to 0 it means we're already calculating. */
    if (!is_all_zeros(_oob_local_random)) {
        status = _pal.generate_secure_connections_oob();

        if (status == BLE_ERROR_NONE) {
            _oob_local_address = *address;
            /* this will be updated when calculation completes,
             * a value of all zeros is an invalid random value */
            set_all_zeros(_oob_local_random);
        } else if (status != BLE_ERROR_NOT_IMPLEMENTED) {
            return status;
        }
    } else {
        return BLE_STACK_BUSY;
    }

    return BLE_ERROR_NONE;
}

ble_error_t GenericSecurityManager::setOOBDataUsage(
    connection_handle_t connection,
    bool useOOB,
    bool OOBProvidesMITM
) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return BLE_ERROR_INVALID_PARAM;
    }

    cb->attempt_oob = useOOB;
    cb->oob_mitm_protection = OOBProvidesMITM;

    if (useOOB) {
        return generateOOB(&cb->local_address);
    } else {
        return BLE_ERROR_NONE;
    }
}

ble_error_t GenericSecurityManager::confirmationEntered(
    connection_handle_t connection,
    bool confirmation
) {
    return _pal.confirmation_entered(connection, confirmation);
}

ble_error_t GenericSecurityManager::passkeyEntered(
    connection_handle_t connection,
    Passkey_t passkey
) {
    return _pal.passkey_request_reply(
        connection,
        PasskeyAscii::to_num(passkey)
    );
}

ble_error_t GenericSecurityManager::sendKeypressNotification(
    connection_handle_t connection,
    Keypress_t keypress
) {
    return _pal.send_keypress_notification(connection, keypress);
}

ble_error_t GenericSecurityManager::legacyPairingOobReceived(
    const address_t *address,
    const oob_tk_t *tk
) {
    if (address && tk) {
        ControlBlock_t *cb = get_control_block(*address);
        if (!cb) {
            return BLE_ERROR_INVALID_PARAM;
        }

        _oob_temporary_key = *tk;
        _oob_temporary_key_creator_address = *address;

        if (cb->peer_address == _oob_temporary_key_creator_address) {
            cb->attempt_oob = true;
        }

        if (cb->legacy_pairing_oob_request_pending) {
            on_legacy_pairing_oob_request(cb->connection);
            /* legacy_pairing_oob_request_pending stops us from
             * going into a loop of asking the user for oob
             * so this reset needs to happen after the call above */
            cb->legacy_pairing_oob_request_pending = false;
        }
    }
    return BLE_ERROR_NONE;
}

ble_error_t GenericSecurityManager::oobReceived(
    const address_t *address,
    const oob_lesc_value_t *random,
    const oob_confirm_t *confirm
) {
    if (address && random && confirm) {
        _oob_peer_address = *address;
        _oob_peer_random = *random;
        _oob_peer_confirm = *confirm;
        return BLE_ERROR_NONE;
    }

    return BLE_ERROR_INVALID_PARAM;
}

////////////////////////////////////////////////////////////////////////////
// Helper functions
//

ble_error_t GenericSecurityManager::init_signing() {
    const csrk_t *pcsrk = _db.get_local_csrk();
    sign_count_t local_sign_counter = _db.get_local_sign_counter();

    if (!pcsrk) {
        csrk_t csrk;

        ble_error_t ret = get_random_data(csrk.data(), csrk.size());
        if (ret != BLE_ERROR_NONE) {
            return ret;
        }

        pcsrk = &csrk;
        _db.set_local_csrk(csrk);
        _db.set_local_sign_counter(local_sign_counter);
    }

    return _pal.set_csrk(*pcsrk, local_sign_counter);
}

ble_error_t GenericSecurityManager::get_random_data(uint8_t *buffer, size_t size) {
    byte_array_t<8> random_data;

    while (size) {
        /* fill out the buffer by reading the random data in chunks
         * and copying it until reaching the set size */
        size_t copy_size = std::max(size, random_data.size());
        ble_error_t ret = _pal.get_random_data(random_data);
        if (ret != BLE_ERROR_NONE) {
            return ret;
        }
        memcpy(buffer, random_data.data(), copy_size);
        size -= copy_size;
        buffer += copy_size;
    }

    return BLE_ERROR_NONE;
}

ble_error_t GenericSecurityManager::slave_security_request(connection_handle_t connection) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return BLE_ERROR_INVALID_PARAM;
    }
    AuthenticationMask link_authentication(_default_authentication);
    link_authentication.set_mitm(cb->mitm_requested);
    return _pal.slave_security_request(connection, link_authentication);
}

ble_error_t GenericSecurityManager::enable_encryption(connection_handle_t connection) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return BLE_ERROR_INVALID_PARAM;
    }
    if (cb->is_master) {
        if (cb->ltk_stored) {
            _db.get_entry_peer_keys(
                mbed::callback(this, &GenericSecurityManager::enable_encryption_cb),
                cb->db_entry
            );
            return BLE_ERROR_NONE;
        } else {
            return requestPairing(connection);
        }
    } else {
        return slave_security_request(connection);
    }
}

void GenericSecurityManager::enable_encryption_cb(
    pal::SecurityDb::entry_handle_t db_entry,
    const SecurityEntryKeys_t* entryKeys
) {
    ControlBlock_t *cb = get_control_block(db_entry);

    if (cb && entryKeys) {
        if (cb->secure_connections_paired) {
            _pal.enable_encryption(cb->connection, entryKeys->ltk, cb->ltk_mitm_protected);
        } else {
            _pal.enable_encryption(cb->connection, entryKeys->ltk, entryKeys->rand, entryKeys->ediv, cb->ltk_mitm_protected);
        }
    }
}

void GenericSecurityManager::set_ltk_cb(
    pal::SecurityDb::entry_handle_t db_entry,
    const SecurityEntryKeys_t* entryKeys
) {
    ControlBlock_t *cb = get_control_block(db_entry);

    if (cb) {
        if (entryKeys) {
            _pal.set_ltk(
                cb->connection,
                entryKeys->ltk,
                cb->ltk_mitm_protected,
                cb->secure_connections_paired
            );
        } else {
            _pal.set_ltk_not_found(cb->connection);
        }
    }
}

void GenericSecurityManager::set_peer_csrk_cb(
    pal::SecurityDb::entry_handle_t db_entry,
    const csrk_t *csrk,
    sign_count_t sign_counter
) {
    ControlBlock_t *cb = get_control_block(db_entry);
    if (!cb) {
        return;
    }

    _pal.set_peer_csrk(
        cb->connection,
        *csrk,
        cb->csrk_mitm_protected,
        sign_counter
    );
}

void GenericSecurityManager::return_csrk_cb(
    pal::SecurityDb::entry_handle_t db_entry,
    const csrk_t *csrk,
    sign_count_t sign_counter
) {
    ControlBlock_t *cb = get_control_block(db_entry);
    if (!cb) {
        return;
    }

    eventHandler->signingKey(
        cb->connection,
        csrk,
        cb->csrk_mitm_protected
    );
}

void GenericSecurityManager::update_oob_presence(connection_handle_t connection) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return;
    }

    /* if we support secure connection we only care about secure connections oob data */
    if (_default_authentication.get_secure_connections()) {
        cb->oob_present = (cb->peer_address == _oob_peer_address);
    } else {
        /* otherwise for legacy pairing we first set the oob based on set preference */
        cb->oob_present = cb->attempt_oob;

        /* and also turn it on if we have oob data for legacy pairing */
        if (cb->peer_address == _oob_temporary_key_creator_address
            || cb->local_address == _oob_temporary_key_creator_address) {
            cb->oob_present = true;
        }
    }
}

void GenericSecurityManager::set_mitm_performed(connection_handle_t connection, bool enable) {
    ControlBlock_t *cb = get_control_block(connection);
    if (cb) {
        cb->mitm_performed = enable;
        /* whenever we reset mitm performed we also reset pending requests
         * as this happens whenever a new pairing attempt happens */
        if (!enable) {
            cb->legacy_pairing_oob_request_pending = false;
        }
    }
}

void GenericSecurityManager::on_connected(
    connection_handle_t connection,
    Gap::Role_t role,
    BLEProtocol::AddressType_t peer_address_type,
    const BLEProtocol::AddressBytes_t peer_address,
    BLEProtocol::AddressType_t local_address_type,
    const BLEProtocol::AddressBytes_t local_address,
    const Gap::ConnectionParams_t *connection_params
) {
    ControlBlock_t *cb = acquire_control_block(connection);
    if (!cb) {
        return;
    }

    // setup the control block
    cb->peer_address = peer_address;
    cb->local_address = local_address;
    cb->peer_address_is_public =
        (peer_address_type == BLEProtocol::AddressType::PUBLIC);
    cb->is_master = (role == Gap::CENTRAL);

    // get the associated db handle and the distribution flags if any
    cb->db_entry = _db.open_entry(peer_address_type, peer_address);

    const pal::SecurityDistributionFlags_t* dist_flags =
        _db.get_distribution_flags(cb->db_entry);

    if (dist_flags) {
        *static_cast<pal::SecurityDistributionFlags_t*>(cb) = *dist_flags;
    }

    const bool signing = cb->signing_override_default ?
                         cb->signing_requested :
                         _default_key_distribution.get_signing();

    if (signing && cb->csrk_stored) {
        _db.get_entry_peer_csrk(
            mbed::callback(this, &GenericSecurityManager::set_peer_csrk_cb),
            cb->db_entry
        );
    }
}

void GenericSecurityManager::on_disconnected(
    connection_handle_t connection,
    Gap::DisconnectionReason_t reason
) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return;
    }

    _pal.remove_peer_csrk(connection);

    _db.close_entry(cb->db_entry);
    release_control_block(cb);

    _db.sync();
}

void GenericSecurityManager::on_security_entry_retrieved(
    pal::SecurityDb::entry_handle_t entry,
    const pal::SecurityEntryIdentity_t* identity
) {
    if (!identity) {
        return;
    }

    typedef advertising_peer_address_type_t address_type_t;

    _pal.add_device_to_resolving_list(
        identity->identity_address_is_public ?
            address_type_t::PUBLIC_ADDRESS :
            address_type_t::RANDOM_ADDRESS,
        identity->identity_address,
        identity->irk
    );
}

void GenericSecurityManager::on_identity_list_retrieved(
    ble::ArrayView<pal::SecurityEntryIdentity_t*>& identity_list,
    size_t count
) {
    typedef advertising_peer_address_type_t address_type_t;

    _pal.clear_resolving_list();
    for (size_t i = 0; i < count; ++i) {
        _pal.add_device_to_resolving_list(
            identity_list[i]->identity_address_is_public ?
                address_type_t::PUBLIC_ADDRESS :
                address_type_t::RANDOM_ADDRESS,
            identity_list[i]->identity_address,
            identity_list[i]->irk
        );
    }

    delete [] identity_list.data();
}


/* Implements ble::pal::SecurityManagerEventHandler */

////////////////////////////////////////////////////////////////////////////
// Pairing
//

void GenericSecurityManager::on_pairing_request(
    connection_handle_t connection,
    bool use_oob,
    AuthenticationMask authentication,
    KeyDistribution initiator_dist,
    KeyDistribution responder_dist
) {
    /* cancel pairing if secure connection paring is not possible */
    if (!_legacy_pairing_allowed && !authentication.get_secure_connections()) {
        cancelPairingRequest(connection);
    }

    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return;
    }

    cb->set_initiator_key_distribution(initiator_dist);
    cb->set_responder_key_distribution(responder_dist);

    cb->mitm_performed = false;

    if (_pairing_authorisation_required) {
        eventHandler->pairingRequest(connection);
    } else {
        acceptPairingRequest(connection);
    }
}

void GenericSecurityManager::on_pairing_error(
    connection_handle_t connection,
    pairing_failure_t error
) {
    set_mitm_performed(connection, false);

    eventHandler->pairingResult(
        connection,
        (SecurityManager::SecurityCompletionStatus_t)(error.value() | 0x80)
    );

    /* if this pairing was triggered by a failed encryption attempt
     * inform the application of the encryption failure */
    ControlBlock_t *cb = get_control_block(connection);
    if (cb && cb->encryption_requested && cb->encryption_failed) {
        eventHandler->linkEncryptionResult(
            connection,
            link_encryption_t::NOT_ENCRYPTED
        );
    }
}

void GenericSecurityManager::on_pairing_timed_out(connection_handle_t connection) {
    set_mitm_performed(connection, false);

    eventHandler->pairingResult(
        connection,
        SecurityManager::SEC_STATUS_TIMEOUT
    );
}

void GenericSecurityManager::on_pairing_completed(connection_handle_t connection) {
    ControlBlock_t *cb = get_control_block(connection);
    if (cb) {
        // set the distribution flags in the db
        _db.set_distribution_flags(cb->db_entry, *cb);
        _db.get_entry_identity(
            mbed::callback(this, &GenericSecurityManager::on_security_entry_retrieved),
            cb->db_entry
        );
    }

    eventHandler->pairingResult(
        connection,
        SecurityManager::SEC_STATUS_SUCCESS
    );
}

////////////////////////////////////////////////////////////////////////////
// Security
//

void GenericSecurityManager::on_valid_mic_timeout(connection_handle_t connection) {
    (void)connection;
}

void GenericSecurityManager::on_signed_write_received(
    connection_handle_t connection,
    sign_count_t sign_counter
) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return;
    }
    _db.set_entry_peer_sign_counter(cb->db_entry, sign_counter);
}

void GenericSecurityManager::on_signed_write_verification_failure(
    connection_handle_t connection
) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return;
    }

    const bool signing = cb->signing_override_default ?
                         cb->signing_requested :
                         _default_key_distribution.get_signing();

    if (signing) {
        cb->csrk_failures++;
        if (cb->csrk_failures == 3) {
            cb->csrk_failures = 0;
            if (cb->is_master) {
                requestPairing(connection);
            } else {
                slave_security_request(connection);
            }
        }
    }
}

void GenericSecurityManager::on_signed_write() {
    _db.set_local_sign_counter(_db.get_local_sign_counter() + 1);
}

void GenericSecurityManager::on_slave_security_request(
    connection_handle_t connection,
    AuthenticationMask authentication
) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return;
    }

    bool pairing_required = false;

    if (authentication.get_secure_connections() && !cb->secure_connections_paired
        && _default_authentication.get_secure_connections()) {
        pairing_required = true;
    }

    if (authentication.get_mitm() && !cb->ltk_mitm_protected) {
        pairing_required = true;
        cb->mitm_requested = true;
    }

    if (pairing_required) {
        requestPairing(connection);
    } else if (!cb->encryption_requested) {
        /* this will refresh keys if encryption is already present */
        enable_encryption(connection);
    }
}

////////////////////////////////////////////////////////////////////////////
// Encryption
//

void GenericSecurityManager::on_link_encryption_result(
    connection_handle_t connection,
    link_encryption_t result
) {

    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return;
    }

    if (result == link_encryption_t::ENCRYPTED) {

        cb->encryption_requested = false;
        cb->encryption_failed = false;
        cb->encrypted = true;

    } else if (
        result == link_encryption_t::ENCRYPTED_WITH_MITM ||
        result == link_encryption_t::ENCRYPTED_WITH_SC_AND_MITM
    ) {

        cb->encryption_requested = false;
        cb->encryption_failed = false;
        cb->authenticated = true;
        cb->encrypted = true;

    } else if (result == link_encryption_t::NOT_ENCRYPTED
               && cb->encryption_requested
               && !cb->encryption_failed) {

        /* if we failed encryption for the first time
         * retry repairing in case slave lost LTK */
        requestPairing(cb->connection);
        cb->encryption_failed = true;
        /* don't return an event yet since we are retrying */
        return;
    }

    eventHandler->linkEncryptionResult(connection, result);
}

void GenericSecurityManager::on_link_encryption_request_timed_out(
    connection_handle_t connection
) {
    eventHandler->linkEncryptionResult(
        connection,
        link_encryption_t::NOT_ENCRYPTED
    );
}

////////////////////////////////////////////////////////////////////////////
// MITM
//

void GenericSecurityManager::on_passkey_display(
    connection_handle_t connection,
    passkey_num_t passkey
) {
    set_mitm_performed(connection);
    eventHandler->passkeyDisplay(connection, PasskeyAscii(passkey).value());
}

void GenericSecurityManager::on_keypress_notification(
    connection_handle_t connection,
    SecurityManager::Keypress_t keypress
) {
    set_mitm_performed(connection);
    eventHandler->keypressNotification(connection, keypress);
}

void GenericSecurityManager::on_passkey_request(connection_handle_t connection) {
    set_mitm_performed(connection);
    eventHandler->passkeyRequest(connection);
}

void GenericSecurityManager::on_confirmation_request(connection_handle_t connection) {
    set_mitm_performed(connection);
    eventHandler->confirmationRequest(connection);
}

void GenericSecurityManager::on_secure_connections_oob_request(connection_handle_t connection) {
    set_mitm_performed(connection);

    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return;
    }

    if (cb->peer_address == _oob_peer_address) {
        _pal.secure_connections_oob_request_reply(connection, _oob_local_random, _oob_peer_random, _oob_peer_confirm);
        /* do not re-use peer OOB */
        set_all_zeros(_oob_peer_address);
    } else {
        _pal.cancel_pairing(connection, pairing_failure_t::OOB_NOT_AVAILABLE);
    }
}

void GenericSecurityManager::on_legacy_pairing_oob_request(connection_handle_t connection) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return;
    }

    if (cb->peer_address == _oob_temporary_key_creator_address
        || cb->local_address == _oob_temporary_key_creator_address) {

        set_mitm_performed(connection);
        _pal.legacy_pairing_oob_request_reply(connection, _oob_temporary_key);

        /* do not re-use peer OOB */
        if (cb->peer_address == _oob_temporary_key_creator_address) {
            set_all_zeros(_oob_temporary_key_creator_address);
        }

    } else if (!cb->legacy_pairing_oob_request_pending) {

        cb->legacy_pairing_oob_request_pending = true;
        eventHandler->legacyPairingOobRequest(connection);

    }
}

void GenericSecurityManager::on_secure_connections_oob_generated(
    const oob_lesc_value_t &random,
    const oob_confirm_t &confirm
) {
    eventHandler->oobGenerated(&_oob_local_address, &random, &confirm);
    _oob_local_random = random;
}

////////////////////////////////////////////////////////////////////////////
// Keys
//

void GenericSecurityManager::on_secure_connections_ltk_generated(
    connection_handle_t connection,
    const ltk_t &ltk
) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return;
    }

    cb->ltk_mitm_protected = cb->mitm_performed;
    cb->secure_connections_paired = true;
    cb->ltk_stored = true;

    _db.set_entry_peer_ltk(cb->db_entry, ltk);
}

void GenericSecurityManager::on_keys_distributed_ltk(
    connection_handle_t connection,
    const ltk_t &ltk
) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return;
    }
    cb->ltk_mitm_protected = cb->mitm_performed;
    cb->ltk_stored = true;
    _db.set_entry_peer_ltk(cb->db_entry, ltk);
}

void GenericSecurityManager::on_keys_distributed_ediv_rand(
    connection_handle_t connection,
    const ediv_t &ediv,
    const rand_t &rand
) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return;
    }

    _db.set_entry_peer_ediv_rand(cb->db_entry, ediv, rand);
}

void GenericSecurityManager::on_keys_distributed_local_ltk(
    connection_handle_t connection,
    const ltk_t &ltk
) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return;
    }

    _db.set_entry_local_ltk(cb->db_entry, ltk);
}

void GenericSecurityManager::on_keys_distributed_local_ediv_rand(
    connection_handle_t connection,
    const ediv_t &ediv,
    const rand_t &rand
) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return;
    }

    _db.set_entry_local_ediv_rand(cb->db_entry, ediv, rand);
}

void GenericSecurityManager::on_keys_distributed_irk(
    connection_handle_t connection,
    const irk_t &irk
) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return;
    }

    _db.set_entry_peer_irk(cb->db_entry, irk);
}

void GenericSecurityManager::on_keys_distributed_bdaddr(
    connection_handle_t connection,
    advertising_peer_address_type_t peer_address_type,
    const address_t &peer_identity_address
) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return;
    }

    _db.set_entry_peer_bdaddr(
        cb->db_entry,
        (peer_address_type == advertising_peer_address_type_t::PUBLIC_ADDRESS),
        peer_identity_address
    );
}

void GenericSecurityManager::on_keys_distributed_csrk(
    connection_handle_t connection,
    const csrk_t &csrk
) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return;
    }

    cb->csrk_mitm_protected = cb->mitm_performed;
    cb->csrk_stored = true;

    _db.set_entry_peer_csrk(cb->db_entry, csrk);

    eventHandler->signingKey(
        connection,
        &csrk,
        cb->csrk_mitm_protected
    );
}

void GenericSecurityManager::on_ltk_request(
    connection_handle_t connection,
    const ediv_t &ediv,
    const rand_t &rand
) {
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return;
    }

    _db.get_entry_local_keys(
        mbed::callback(this, &GenericSecurityManager::set_ltk_cb),
        cb->db_entry,
        ediv,
        rand
    );
}

/* control blocks list management */

GenericSecurityManager::ControlBlock_t::ControlBlock_t() :
    pal::SecurityDistributionFlags_t(),
    connection(0),
    db_entry(0),
    local_address(),
    connected(false),
    authenticated(false),
    is_master(false),
    encryption_requested(false),
    encryption_failed(false),
    encrypted(false),
    signing_requested(false),
    signing_override_default(false),
    mitm_requested(false),
    mitm_performed(false),
    attempt_oob(false),
    oob_mitm_protection(false),
    oob_present(false),
    legacy_pairing_oob_request_pending(false),
    csrk_failures(0) { }

void GenericSecurityManager::on_ltk_request(connection_handle_t connection)
{
    ControlBlock_t *cb = get_control_block(connection);
    if (!cb) {
        return;
    }

    _db.get_entry_local_keys(
        mbed::callback(this, &GenericSecurityManager::set_ltk_cb),
        cb->db_entry
    );
}

GenericSecurityManager::ControlBlock_t*
GenericSecurityManager::acquire_control_block(connection_handle_t connection)
{
    /* grab the first disconnected slot*/
    for (size_t i = 0; i < MAX_CONTROL_BLOCKS; i++) {
        if (!_control_blocks[i].connected) {
            ControlBlock_t* cb = &_control_blocks[i];
            cb->connected = true;
            cb->connection = connection;
            return cb;
        }
    }

    return NULL;
}

GenericSecurityManager::ControlBlock_t* GenericSecurityManager::get_control_block(
    connection_handle_t connection
) {
    for (size_t i = 0; i < MAX_CONTROL_BLOCKS; i++) {
        if (!_control_blocks[i].connected) {
            continue;
        } else if (connection == _control_blocks[i].connection) {
            return &_control_blocks[i];
        }
    }
    return NULL;
}

GenericSecurityManager::ControlBlock_t* GenericSecurityManager::get_control_block(
    const address_t &peer_address
) {
    for (size_t i = 0; i < MAX_CONTROL_BLOCKS; i++) {
        if (!_control_blocks[i].connected) {
            continue;
        } else if (peer_address == _control_blocks[i].peer_address) {
            return &_control_blocks[i];
        }
    }
    return NULL;
}

GenericSecurityManager::ControlBlock_t* GenericSecurityManager::get_control_block(
    pal::SecurityDb::entry_handle_t db_entry
) {
    for (size_t i = 0; i < MAX_CONTROL_BLOCKS; i++) {
        if (!_control_blocks[i].connected) {
            continue;
        } else if (db_entry == _control_blocks[i].db_entry) {
            return &_control_blocks[i];
        }
    }
    return NULL;
}

void GenericSecurityManager::release_control_block(ControlBlock_t* cb)
{
    *cb = ControlBlock_t();
}

} /* namespace generic */
} /* namespace ble */
