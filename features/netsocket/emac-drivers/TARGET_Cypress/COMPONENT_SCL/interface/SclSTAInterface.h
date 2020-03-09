/*
 * (c) (2019-2020), Cypress Semiconductor Corporation. All rights reserved.
 *
 * This software, including source code, documentation and related materials
 * ("Software"), is owned by Cypress Semiconductor Corporation or one of its
 * subsidiaries ("Cypress") and is protected by and subject to worldwide patent
 * protection (United States and foreign), United States copyright laws and
 * international treaty provisions. Therefore, you may use this Software only
 * as provided in the license agreement accompanying the software package from
 * which you obtained this Software ("EULA").
 *
 * If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
 * non-transferable license to copy, modify, and compile the Software source
 * code solely for use in connection with Cypress' integrated circuit products.
 * Any reproduction, modification, translation, compilation, or representation
 * of this Software except as specified above is prohibited without the express
 * written permission of Cypress.
 *
 * Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
 * reserves the right to make changes to the Software without notice. Cypress
 * does not assume any liability arising out of the application or use of the
 * Software or any product or circuit described in the Software. Cypress does
 * not authorize its products for use in any products where a malfunction or
 * failure of the Cypress product may reasonably be expected to result in
 * significant property damage, injury or death ("High Risk Product"). By
 * including Cypress' product in a High Risk Product, the manufacturer of such
 * system or application assumes all risk of such use and in doing so agrees to
 * indemnify Cypress against all liability.
 */

#ifndef SCL_STA_INTERFACE_H
#define SCL_STA_INTERFACE_H


/** @file
 *  Provides SCL interface functions to be used with WiFiInterface or NetworkInterface Objects
 */

#include "netsocket/WiFiInterface.h"
#include "netsocket/EMACInterface.h"
#include "netsocket/OnboardNetworkStack.h"
#include "scl_emac.h"
#include "scl_wifi_api.h"
#include "scl_types.h"
#define MAX_SSID_LENGTH                       (33) /**< Maximum ssid length */
#define MAX_PASSWORD_LENGTH                   (64) /**< Maximum password length */

/** SclSTAInterface class
 *  Implementation of the Network Stack for the SCL
 */
class SclSTAInterface : public WiFiInterface, public EMACInterface {
public:

    SclSTAInterface(
        SCL_EMAC &emac = SCL_EMAC::get_instance(),
        OnboardNetworkStack &stack = OnboardNetworkStack::get_default_instance());
    
    /** Gets the current instance of the SclSTAInterface
     *
     *  @return         Pointer to the object of class SclSTAInterface.
     */
	static SclSTAInterface *get_default_instance();
    
	/** Turns on the Wi-Fi device
    *
    *  @return         void
    */
    void wifi_on();

    /** Starts the interface
     *
     *  Attempts to connect to a Wi-Fi network. Requires ssid and passphrase to be set.
     *  If passphrase is invalid, NSAPI_ERROR_AUTH_ERROR is returned.
     *
     *  @return         0 on success, negative error code on failure.
     */
    nsapi_error_t connect();

    /** Starts the interface
     *
     *  Attempts to connect to a Wi-Fi network.
     *
     *  @param ssid      Name of the network to connect to.
     *  @param pass      Security passphrase to connect to the network.
     *  @param security  Type of encryption for connection (Default: NSAPI_SECURITY_NONE).
     *  @param channel   This parameter is not supported, setting it to a value other than 0 will result in NSAPI_ERROR_UNSUPPORTED.
     *  @return          0 on success, negative error code on failure.
     */
    nsapi_error_t connect(const char *ssid, const char *pass, nsapi_security_t security = NSAPI_SECURITY_NONE, uint8_t channel = 0);

    /** Disconnects the interface
     *
     *  @return             0 on success, negative error code on failure.
     */
    nsapi_error_t disconnect();

    /** Set the Wi-Fi network credentials
     *
     *  @param ssid      Name of the network to connect to.
     *  @param pass      Security passphrase to connect to the network.
     *  @param security  Type of encryption for connection.
     *                   (defaults to NSAPI_SECURITY_NONE)
     *  @return          0 on success, negative error code on failure.
     */
    nsapi_error_t set_credentials(const char *ssid, const char *pass, nsapi_security_t security = NSAPI_SECURITY_NONE);

    /** Sets the Wi-Fi network channel - NOT SUPPORTED
     *
     *  This function is not supported and will return NSAPI_ERROR_UNSUPPORTED.
     *
     *  @param channel   Channel on which the connection is to be made (Default: 0).
     *  @return          Not supported, returns NSAPI_ERROR_UNSUPPORTED.
     */
    nsapi_error_t set_channel(uint8_t channel)
    {
        if (channel != 0) {
            return NSAPI_ERROR_UNSUPPORTED;
        }
        return 0;
    }

    /** Set blocking status of interface. 
     *  Nonblocking mode is not supported.
     *
     *  @param blocking  True if connect is blocking
     *  @return          0 on success, negative error code on failure
     */
    nsapi_error_t set_blocking(bool blocking)
    {
        if (blocking) {
            _blocking = blocking;
            return NSAPI_ERROR_OK;
        } else {
            return NSAPI_ERROR_UNSUPPORTED;
        }
    }
    /** Gets the current radio signal strength for active connection
     *
     *  @return          Connection strength in dBm (negative value).
     */
    int8_t get_rssi();

    /** Scans for available networks - NOT SUPPORTED
     *
     *  @return         NSAPI_ERROR_UNSUPPORTED
     */
    int scan(WiFiAccessPoint *res, unsigned count);

    /** This function is used to indicate if the device is connected to the network.
     *
     *  @return          SCL_SUCCESS if device is connected.
     */
    int is_interface_connected();

    /** Gets the BSSID (MAC address of device connected to).
     *
     *  @param bssid   Pointer to the BSSID value.
     *  @return        SCL_SUCCESS if BSSID is obtained successfully.
     *  @return        SCL_BADARG if input parameter is NULL.
     *  @return        SCL_ERROR if unable to fetch BSSID.
     */
    int get_bssid(uint8_t *bssid);

    /** This function is used to set up the Wi-Fi interface.
     *  This function should be used after the wifi_on.
     *
     * @return          SCL_SUCCESS if the Wi-Fi interface is set up successfully.
     */
    int wifi_set_up(void);

private:

    char _ssid[MAX_SSID_LENGTH]; /**< The longest possible name (defined in 802.11) +1 for the \0 */
    char _pass[MAX_PASSWORD_LENGTH]; /**< The longest allowed passphrase + 1 */
    nsapi_security_t _security; /**< Security type */
    SCL_EMAC &_scl_emac; /**< SCL_EMAC object */
};
#endif /* ifndef SCL_STA_INTERFACE_H */
