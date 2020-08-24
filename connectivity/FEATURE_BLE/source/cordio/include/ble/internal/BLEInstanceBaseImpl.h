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

#ifndef IMPL_BLE_INSTANCE_BASE_H_
#define IMPL_BLE_INSTANCE_BASE_H_

#include "ble/BLE.h"
#include "ble/common/blecommon.h"
#include "source/BLEInstanceBase.h"

#include "ble/driver/CordioHCIDriver.h"
#include "ble/GattServer.h"
#include "source/pal/PalAttClient.h"
#include "source/pal/PalGattClient.h"
#include "ble/GattClient.h"
#include "source/pal/PalGap.h"
#include "ble/Gap.h"
#include "source/pal/PalGenericAccessService.h"
#include "ble/SecurityManager.h"
#include "source/pal/PalEventQueue.h"
#include "drivers/LowPowerTimer.h"
#include "source/pal/PalSecurityManager.h"

#include "source/generic/GapImpl.h"
#include "source/generic/GattClientImpl.h"
#include "source/GattServerImpl.h"
#include "source/generic/SecurityManagerImpl.h"
#include "internal/PalEventQueueImpl.h"

namespace ble {

class PalSigningMonitor;

namespace impl {

/**
 * @see BLEInstanceBase
 */
class BLEInstanceBase : public ble::BLEInstanceBase {
    friend PalSigningMonitor;

    /**
     * Construction with an HCI driver.
     */
    BLEInstanceBase(CordioHCIDriver &hci_driver);

    /**
     * Destructor
     */
    virtual ~BLEInstanceBase();

public:

    /**
     * Access to the singleton containing the implementation of BLEInstanceBase
     * for the Cordio stack.
     */
    static BLEInstanceBase &deviceInstance();

    /**
    * @see BLEInstanceBase::init
    */
    virtual ble_error_t init(
        FunctionPointerWithContext<::BLE::InitializationCompleteCallbackContext *> initCallback
    );

    /**
     * @see BLEInstanceBase::hasInitialized
     */
    virtual bool hasInitialized() const;

    /**
     * @see BLEInstanceBase::shutdown
     */
    virtual ble_error_t shutdown();

    /**
     * @see BLEInstanceBase::getVersion
     */
    virtual const char *getVersion();

    ble::impl::Gap &getGapImpl();

    /**
     * @see BLEInstanceBase::getGap
     */
    virtual ble::Gap &getGap();

    /**
     * @see BLEInstanceBase::getGap
     */
    virtual const ble::Gap &getGap() const;

#if BLE_FEATURE_GATT_SERVER

    ble::impl::GattServer &getGattServerImpl();

    /**
     * @see BLEInstanceBase::getGattServer
     */
    virtual ble::GattServer &getGattServer();

    /**
     * @see BLEInstanceBase::getGattServer
     */
    virtual const ble::GattServer &getGattServer() const;

#endif // BLE_FEATURE_GATT_SERVER

#if BLE_FEATURE_GATT_CLIENT

    ble::impl::GattClient &getGattClientImpl();

    /**
     * @see BLEInstanceBase::getGattClient
     */
    virtual ble::GattClient &getGattClient();

    /**
     * Get the PAL Gatt Client.
     *
     * @return PAL Gatt Client.
     */
    PalGattClient &getPalGattClient();

#endif // BLE_FEATURE_GATT_CLIENT

#if BLE_FEATURE_SECURITY

    ble::impl::SecurityManager &getSecurityManagerImpl();

    /**
     * @see BLEInstanceBase::getSecurityManager
     */
    virtual ble::SecurityManager &getSecurityManager();

    /**
     * @see BLEInstanceBase::getSecurityManager
     */
    virtual const ble::SecurityManager &getSecurityManager() const;

#endif // BLE_FEATURE_SECURITY

    /**
     * @see BLEInstanceBase::waitForEvent
     */
    virtual void waitForEvent();

    /**
     * @see BLEInstanceBase::processEvents
     */
    virtual void processEvents();

private:
    static void stack_handler(wsfEventMask_t event, wsfMsgHdr_t *msg);

    static void device_manager_cb(dmEvt_t *dm_event);

    static void connection_handler(dmEvt_t *dm_event);

    static void timeoutCallback();

    void stack_setup();

    void start_stack_reset();

    void callDispatcher();

    static CordioHCIDriver *_hci_driver;
    static FunctionPointerWithContext<::BLE::InitializationCompleteCallbackContext *> _init_callback;

    enum {
        NOT_INITIALIZED,
        INITIALIZING,
        INITIALIZED
    } initialization_status;

    mutable ble::impl::PalEventQueue _event_queue;
    mbed::LowPowerTimer _timer;
    uint64_t _last_update_us;
};

} // namespace impl
} // namespace ble


#endif /* IMPL_BLE_INSTANCE_BASE_H_ */
