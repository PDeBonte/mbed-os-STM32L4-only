/*
 * Copyright (c) 2018 ARM Limited. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <string.h>
#include "platform/arm_hal_interrupt.h"
#include "nanostack/platform/arm_hal_phy.h"
#include "ns_types.h"
#include "NanostackRfPhys2lp.h"
#include "s2lpReg.h"
#include "rf_configuration.h"
#include "randLIB.h"
#include "mbed_trace.h"
#include "mbed_toolchain.h"
#include "common_functions.h"
#include <Timer.h>

#define TRACE_GROUP "s2lp"

#define INTERRUPT_GPIO  S2LP_GPIO3

#if INTERRUPT_GPIO == S2LP_GPIO0
#define INT_IN_GPIO     rf->GPIO0
#elif INTERRUPT_GPIO == S2LP_GPIO1
#define INT_IN_GPIO     rf->GPIO1
#elif INTERRUPT_GPIO == S2LP_GPIO2
#define INT_IN_GPIO     rf->GPIO2
#else
#define INT_IN_GPIO     rf->GPIO3
#endif

#ifdef TEST_GPIOS_ENABLED
#define TEST_TX_STARTED     rf->TEST1 = 1;
#define TEST_TX_DONE        rf->TEST1 = 0;
#define TEST_RX_STARTED     rf->TEST2 = 1;
#define TEST_RX_DONE        rf->TEST2 = 0;
#define TEST_ACK_TX_STARTED rf->TEST3 = 1;
#define TEST_ACK_TX_DONE    rf->TEST3 = 0;
#define TEST1_ON            rf->TEST4 = 1;
#define TEST1_OFF           rf->TEST4 = 0;
#define TEST2_ON            rf->TEST5 = 1;
#define TEST2_OFF           rf->TEST5 = 0;
extern void (*fhss_uc_switch)(void);
extern void (*fhss_bc_switch)(void);
#else //TEST_GPIOS_ENABLED
#define TEST_TX_STARTED
#define TEST_TX_DONE
#define TEST_RX_STARTED
#define TEST_RX_DONE
#define TEST_ACK_TX_STARTED
#define TEST_ACK_TX_DONE
#define TEST1_ON
#define TEST1_OFF
#define TEST2_ON
#define TEST2_OFF
#endif //TEST_GPIOS_ENABLED

#define MAC_FRAME_TYPE_MASK     0x07
#define MAC_FRAME_BEACON        (0)
#define MAC_TYPE_DATA           (1)
#define MAC_TYPE_ACK            (2)
#define MAC_TYPE_COMMAND        (3)
#define MAC_DATA_PENDING        0x10
#define MAC_FRAME_VERSION_2     (2)
#define FC_DST_MODE             0x0C
#define FC_SRC_MODE             0xC0
#define FC_DST_ADDR_NONE        0x00
#define FC_DST_16_BITS          0x08
#define FC_DST_64_BITS          0x0C
#define FC_SRC_64_BITS          0xC0
#define FC_SEQUENCE_COMPRESSION 0x01
#define FC_AR                   0x20
#define FC_PAN_ID_COMPRESSION   0x40
#define VERSION_FIELD_MASK      0x30
#define SHIFT_SEQ_COMP_FIELD    (0)
#define SHIFT_VERSION_FIELD     (4)
#define SHIFT_PANID_COMP_FIELD  (6)
#define OFFSET_DST_PAN_ID       (3)
#define OFFSET_DST_ADDR         (5)

#define CS_SELECT()  {rf->CS = 0;}
#define CS_RELEASE() {rf->CS = 1;}

class UnlockedSPI : public SPI {
public:
    UnlockedSPI(PinName sdi, PinName sdo, PinName sclk) :
        SPI(sdi, sdo, sclk) { }
    virtual void lock() { }
    virtual void unlock() { }
};

class RFPins {
public:
    RFPins(PinName spi_sdi, PinName spi_sdo,
            PinName spi_sclk, PinName spi_cs, PinName spi_sdn,
#ifdef TEST_GPIOS_ENABLED
            PinName spi_test1, PinName spi_test2, PinName spi_test3, PinName spi_test4, PinName spi_test5,
#endif //TEST_GPIOS_ENABLED
            PinName spi_gpio0, PinName spi_gpio1, PinName spi_gpio2,
            PinName spi_gpio3);
    UnlockedSPI spi;
    DigitalOut CS;
    DigitalOut SDN;
#ifdef TEST_GPIOS_ENABLED
    DigitalOut TEST1;
    DigitalOut TEST2;
    DigitalOut TEST3;
    DigitalOut TEST4;
    DigitalOut TEST5;
#endif //TEST_GPIOS_ENABLED
    InterruptIn GPIO0;
    InterruptIn GPIO1;
    InterruptIn GPIO2;
    InterruptIn GPIO3;
    Timeout cca_timer;
    Timeout backup_timer;
    Timer tx_timer;
#ifdef MBED_CONF_RTOS_PRESENT
    Thread irq_thread;
    Mutex mutex;
    void rf_irq_task();
#endif //MBED_CONF_RTOS_PRESENT
};

RFPins::RFPins(PinName spi_sdi, PinName spi_sdo,
        PinName spi_sclk, PinName spi_cs, PinName spi_sdn,
#ifdef TEST_GPIOS_ENABLED
        PinName spi_test1, PinName spi_test2, PinName spi_test3, PinName spi_test4, PinName spi_test5,
#endif //TEST_GPIOS_ENABLED
        PinName spi_gpio0, PinName spi_gpio1, PinName spi_gpio2,
        PinName spi_gpio3)
:   spi(spi_sdi, spi_sdo, spi_sclk),
    CS(spi_cs),
    SDN(spi_sdn),
#ifdef TEST_GPIOS_ENABLED
    TEST1(spi_test1),
    TEST2(spi_test2),
    TEST3(spi_test3),
    TEST4(spi_test4),
    TEST5(spi_test5),
#endif //TEST_GPIOS_ENABLED
    GPIO0(spi_gpio0),
    GPIO1(spi_gpio1),
    GPIO2(spi_gpio2),
    GPIO3(spi_gpio3)
#ifdef MBED_CONF_RTOS_PRESENT
,irq_thread(osPriorityRealtime, 1024)
#endif //MBED_CONF_RTOS_PRESENT
{
#ifdef MBED_CONF_RTOS_PRESENT
    irq_thread.start(mbed::callback(this, &RFPins::rf_irq_task));
#endif //MBED_CONF_RTOS_PRESENT
}

static uint8_t rf_read_register_with_status(uint8_t addr, uint16_t *status_out);
static s2lp_states_e rf_read_state(void);
static void rf_write_register(uint8_t addr, uint8_t data);
static void rf_print_registers(void);
static void rf_interrupt_handler(void);
static void rf_receive(uint8_t rx_channel);
static void rf_cca_timer_stop(void);
static void rf_backup_timer_start(uint32_t slots);
static void rf_backup_timer_stop(void);
static void rf_rx_ready_handler(void);
static uint32_t read_irq_status(void);
static bool rf_rx_filter(uint8_t *mac_header, uint8_t *mac_64bit_addr, uint8_t *mac_16bit_addr, uint8_t *pan_id);

static RFPins *rf;
static phy_device_driver_s device_driver;
static int8_t rf_radio_driver_id = -1;
static uint8_t *tx_data_ptr;
static uint16_t tx_data_length;
static uint16_t rx_data_length;
static uint32_t enabled_interrupts;
static uint8_t mac_tx_handle;
static uint8_t rf_rx_channel;
static uint8_t rf_new_channel;
static uint8_t rx_buffer[RF_MTU];
static rf_states_e rf_state = RF_IDLE;
// This will be used when given channel spacing cannot be supported by transceiver
static uint8_t rf_channel_multiplier = 1;
static uint16_t tx_sequence = 0xffff;
static uint32_t tx_time = 0;
static uint32_t rx_time = 0;
static uint32_t tx_finnish_time = 0;
static uint32_t symbols_in_seconds;
static bool cca_enabled = true;
static uint8_t s2lp_PAN_ID[2] = {0xff, 0xff};
static uint8_t s2lp_short_address[2];
static uint8_t s2lp_MAC[8];

/* Channel configurations for sub-GHz */
static const phy_rf_channel_configuration_s phy_subghz = {868300000U, 500000U, 250000U, 11U, M_UNDEFINED};

static const phy_device_channel_page_s phy_channel_pages[] = {
        { CHANNEL_PAGE_2, &phy_subghz},
        { CHANNEL_PAGE_0, NULL}
};

#ifdef MBED_CONF_RTOS_PRESENT
#include "mbed.h"
#include "rtos.h"

static void rf_irq_task_process_irq();

#define SIG_RADIO           1
#define SIG_TIMER_CCA       2
#define SIG_TIMER_BACKUP    4

#endif //MBED_CONF_RTOS_PRESENT

#define ACK_FRAME_LENGTH    3
// Give some additional time for processing, PHY headers, CRC etc.
#define PACKET_SENDING_EXTRA_TIME   10000
#define MAX_PACKET_SENDING_TIME (uint32_t)(8000000/phy_subghz.datarate)*RF_MTU + PACKET_SENDING_EXTRA_TIME
#define ACK_SENDING_TIME (uint32_t)(8000000/phy_subghz.datarate)*ACK_FRAME_LENGTH + PACKET_SENDING_EXTRA_TIME

#ifdef TEST_GPIOS_ENABLED
void test1_toggle(void)
{
    if (rf->TEST4) {
        rf->TEST4 = 0;
    } else {
        rf->TEST4 = 1;
    }
}
void test2_toggle(void)
{
    if (rf->TEST5) {
        rf->TEST5 = 0;
    } else {
        rf->TEST5 = 1;
    }
}
#endif //TEST_GPIOS_ENABLED

static void rf_calculate_symbols_in_seconds(uint32_t baudrate, phy_modulation_e modulation)
{
    (void) modulation;
    uint8_t bits_in_symbols = 1;
    symbols_in_seconds = baudrate / bits_in_symbols;
}

static uint32_t rf_get_timestamp(void)
{
    return (uint32_t)rf->tx_timer.read_us();
}

static void rf_lock(void)
{
    platform_enter_critical();
}

static void rf_unlock(void)
{
    platform_exit_critical();
}

#ifdef MBED_CONF_RTOS_PRESENT
static void rf_cca_timer_signal(void)
{
    rf->irq_thread.signal_set(SIG_TIMER_CCA);
}

static void rf_backup_timer_signal(void)
{
    rf->irq_thread.signal_set(SIG_TIMER_BACKUP);
}
#endif //MBED_CONF_RTOS_PRESENT

/*
 * \brief Function writes/read data in SPI interface
 */
static void rf_spi_exchange(const void *tx, size_t tx_len, void *rx, size_t rx_len)
{
    rf->spi.write(static_cast<const char *>(tx), tx_len, static_cast<char *>(rx), rx_len);
}

static uint8_t rf_read_register_with_status(uint8_t addr, uint16_t *status_out)
{
    const uint8_t tx[2] = {SPI_RD_REG, addr};
    uint8_t rx[3];
    rf_lock();
    CS_SELECT();
    rf_spi_exchange(tx, sizeof(tx), rx, sizeof(rx));
    CS_RELEASE();
    if (status_out) {
        *status_out = (rx[0] << 8) | rx[1];
    }
    rf_unlock();
    return rx[2];
}

static void rf_write_register(uint8_t addr, uint8_t data)
{
    const uint8_t tx[3] = {SPI_WR_REG, addr, data};
    rf_lock();
    CS_SELECT();
    rf_spi_exchange(tx, sizeof(tx), NULL, 0);
    CS_RELEASE();
    rf_unlock();
}

static void rf_write_register_field(uint8_t addr, uint8_t field, uint8_t value)
{
    uint8_t reg_tmp = rf_read_register_with_status(addr, NULL);
    reg_tmp &= ~field;
    reg_tmp |= value;
    rf_write_register(addr, reg_tmp);
}

static s2lp_states_e rf_read_state(void)
{
    return (s2lp_states_e) (rf_read_register_with_status(MC_STATE0, NULL) >> 1);
}

static void rf_poll_state_change(s2lp_states_e state)
{
    uint16_t break_counter = 0;
    while (state != rf_read_state()) {
        if (break_counter++ == 0xffff) {
            tr_err("Failed to change state from %x to: %x", rf_read_state(), state);
            break;
        }
    }
}

static void rf_enable_gpio_signal(uint8_t gpio_pin, uint8_t interrupt_signal, uint8_t gpio_mode)
{
    rf_write_register(GPIO0_CONF+gpio_pin, (interrupt_signal<<3) | gpio_mode);
}

static void rf_enable_interrupt(uint8_t event)
{
    rf_write_register_field(IRQ_MASK0 - (event / 8), 1 << (event % 8), 1 << (event % 8));
    enabled_interrupts |= (1 << event);
}

static void rf_disable_interrupt(uint8_t event)
{
    rf_write_register_field(IRQ_MASK0 - (event / 8), 1 << (event % 8), 0 << (event % 8));
    enabled_interrupts &= ~(1 << event);
}

static void rf_disable_all_interrupts(void)
{
    rf_write_register(IRQ_MASK0, 0);
    rf_write_register(IRQ_MASK1, 0);
    rf_write_register(IRQ_MASK2, 0);
    rf_write_register(IRQ_MASK3, 0);
    enabled_interrupts = 0;
    read_irq_status();
}

static void rf_enable_gpio_interrupt(void)
{
    rf_enable_gpio_signal(INTERRUPT_GPIO, NIRQ, DIG_OUT_HIGH);
    INT_IN_GPIO.mode(PullUp);
    INT_IN_GPIO.fall(&rf_interrupt_handler);
    INT_IN_GPIO.enable_irq();
}

static void rf_send_command(s2lp_commands_e command)
{
    const uint8_t tx[2] = {SPI_CMD, command};
    rf_lock();
    CS_SELECT();
    rf_spi_exchange(tx, sizeof(tx), NULL, 0);
    CS_RELEASE();
    rf_unlock();
}

static void rf_state_change(s2lp_states_e state, bool lock_mode_tx)
{
    s2lp_commands_e command;

    if (S2LP_STATE_READY == state) {
        s2lp_states_e cur_state = rf_read_state();
        if (S2LP_STATE_TX == cur_state || S2LP_STATE_RX == cur_state) {
            command = S2LP_CMD_SABORT;
        } else {
            command = S2LP_CMD_READY;
        }
    } else if (S2LP_STATE_LOCK == state && lock_mode_tx) {
        command = S2LP_CMD_LOCKTX;
    } else if (S2LP_STATE_LOCK == state && !lock_mode_tx) {
        command = S2LP_CMD_LOCKRX;
    } else if (S2LP_STATE_RX == state) {
        command = S2LP_CMD_RX;
    } else if (S2LP_STATE_TX == state) {
        command = S2LP_CMD_TX;
    } else {
        tr_err("Invalid state %x", state);
        return;
    }
    rf_send_command(command);
    rf_poll_state_change(state);
}

static uint8_t rf_write_tx_fifo(uint8_t *ptr, uint16_t length, uint8_t max_write)
{
    const uint8_t spi_header[SPI_HEADER_LENGTH] = {SPI_WR_REG, TX_FIFO};
    uint8_t written_length = length;
    if (length > max_write) {
        written_length = max_write;
    }
    CS_SELECT();
    rf_spi_exchange(spi_header, SPI_HEADER_LENGTH, NULL, 0);
    rf_spi_exchange(ptr, written_length, NULL, 0);
    CS_RELEASE();
    return written_length;
}

static uint8_t rf_read_rx_fifo(uint8_t *ptr, uint16_t length)
{
    const uint8_t spi_header[SPI_HEADER_LENGTH] = {SPI_RD_REG, RX_FIFO};
    CS_SELECT();
    rf_spi_exchange(spi_header, SPI_HEADER_LENGTH, NULL, 0);
    rf_spi_exchange(NULL, 0, ptr, length);
    CS_RELEASE();
    return length;
}

static void rf_write_packet_length(uint16_t packet_length)
{
    rf_write_register(PCKTLEN1, packet_length/256);
    rf_write_register(PCKTLEN0, packet_length%256);
}

static uint32_t read_irq_status(void)
{
    const uint8_t tx[2] = {SPI_RD_REG, IRQ_STATUS3};
    uint8_t rx[6];
    CS_SELECT();
    rf_spi_exchange(tx, sizeof(tx), rx, sizeof(rx));
    CS_RELEASE();
    return (((uint32_t)rx[2] << 24) | ((uint32_t)rx[3] << 16) | ((uint32_t)rx[4] << 8) | rx[5]);
}

static void rf_init_registers(void)
{
    rf_write_register_field(PCKTCTRL3, PCKT_FORMAT_FIELD, PCKT_FORMAT_802_15_4);
    // Set deviation
    uint8_t fdev_m, fdev_e;
    rf_conf_calculate_deviation_registers(DEVIATION, &fdev_m, &fdev_e);
    rf_write_register(MOD0, fdev_m);
    rf_write_register_field(MOD1, FDEV_E_FIELD, fdev_e);
    rf_write_register_field(MOD2, MOD_TYPE_FIELD, MOD_2FSK);
    // Set datarate
    uint16_t datarate_m;
    uint8_t datarate_e;
    rf_conf_calculate_datarate_registers(phy_subghz.datarate, &datarate_m, &datarate_e);
    rf_write_register_field(MOD2, DATARATE_E_FIELD, datarate_e);
    rf_write_register(MOD3, (uint8_t)datarate_m);
    rf_write_register(MOD4, datarate_m>>8);
    // Set RX filter bandwidth
    uint8_t chflt_m, chflt_e;
    rf_conf_calculate_rx_filter_bandwidth_registers(RX_FILTER_BANDWIDTH, &chflt_m, &chflt_e);
    rf_write_register_field(CHFLT, CHFLT_M_FIELD, chflt_m << 4);
    rf_write_register_field(CHFLT, CHFLT_E_FIELD, chflt_e);
    rf_write_register(PCKT_FLT_OPTIONS, 0);
    // Set base frequency (Channel 0 center frequency)
    uint8_t synt3, synt2, synt1, synt0;
    rf_conf_calculate_base_frequency_registers(phy_subghz.channel_0_center_frequency, &synt3, &synt2, &synt1, &synt0);
    rf_write_register_field(SYNT3, SYNT_FIELD, synt3);
    rf_write_register(SYNT2, synt2);
    rf_write_register(SYNT1, synt1);
    rf_write_register(SYNT0, synt0);
    // Set channel spacing
    uint8_t ch_space;
    uint8_t ch_space_divider = 1;
    while (rf_conf_calculate_channel_spacing_registers(phy_subghz.channel_spacing/ch_space_divider, &ch_space)) {
        ch_space_divider++;
        rf_channel_multiplier++;
    }
    rf_write_register(CHSPACE, ch_space);
    rf_write_register_field(PCKTCTRL1, PCKT_CRCMODE_FIELD, PCKT_CRCMODE_0X1021);
    rf_write_register_field(PCKTCTRL1, PCKT_TXSOURCE_FIELD, PCKT_TXSOURCE_NORMAL);
    rf_write_register_field(PCKTCTRL1, PCKT_WHITENING_FIELD, PCKT_WHITENING_ENABLED);
    rf_write_register_field(PCKTCTRL2, PCKT_FIXVARLEN_FIELD, PCKT_VARIABLE_LEN);
    rf_write_register_field(PCKTCTRL3, PCKT_RXMODE_FIELD, PCKT_RXMODE_NORMAL);
    rf_write_register(PCKTCTRL5, PCKT_PREAMBLE_LEN);
    rf_write_register_field(PCKTCTRL6, PCKT_SYNCLEN_FIELD, PCKT_SYNCLEN);
    rf_write_register_field(QI, PQI_TH_FIELD, PQI_TH);
    rf_write_register_field(QI, SQI_EN_FIELD, SQI_EN);
    rf_write_register(SYNC0, SFD0);
    rf_write_register(SYNC1, SFD1);
    // Set RSSI threshold
    uint8_t rssi_th;
    rf_conf_calculate_rssi_threshold_registers(RSSI_THRESHOLD, &rssi_th);
    rf_write_register(RSSI_TH, rssi_th);
}

static int8_t rf_address_write(phy_address_type_e address_type, uint8_t *address_ptr)
{
    int8_t ret_val = 0;
    switch (address_type) {
    /*Set 48-bit address*/
    case PHY_MAC_48BIT:
        break;
        /*Set 64-bit address*/
    case PHY_MAC_64BIT:
        memcpy(s2lp_MAC, address_ptr, 8);
        break;
        /*Set 16-bit address*/
    case PHY_MAC_16BIT:
        memcpy(s2lp_short_address, address_ptr, 2);
        break;
        /*Set PAN Id*/
    case PHY_MAC_PANID:
        memcpy(s2lp_PAN_ID, address_ptr, 2);
        break;
    }
    return ret_val;
}

static int8_t rf_extension(phy_extension_type_e extension_type, uint8_t *data_ptr)
{
    int8_t retval = 0;
    phy_csma_params_t *csma_params;
    uint32_t *timer_value;
    switch (extension_type) {
    case PHY_EXTENSION_SET_CHANNEL:
        if (rf_state == RF_IDLE || rf_state == RF_CSMA_STARTED) {
            rf_receive(*data_ptr);
        } else {
            // Store the new channel if couldn't change it yet.
            rf_new_channel = *data_ptr;
            retval = -1;
        }
        break;
    case PHY_EXTENSION_CTRL_PENDING_BIT:
        break;
        /*Return frame pending status*/
    case PHY_EXTENSION_READ_LAST_ACK_PENDING_STATUS:
        break;
    case PHY_EXTENSION_ACCEPT_ANY_BEACON:
        break;
    case PHY_EXTENSION_SET_TX_TIME:
        tx_time = common_read_32_bit(data_ptr);
        break;
    case PHY_EXTENSION_READ_RX_TIME:
        common_write_32_bit(rx_time, data_ptr);
        break;
    case PHY_EXTENSION_DYNAMIC_RF_SUPPORTED:
        *data_ptr = true;
        break;
    case PHY_EXTENSION_GET_TIMESTAMP:
        timer_value = (uint32_t*)data_ptr;
        *timer_value = rf_get_timestamp();
        break;
    case PHY_EXTENSION_SET_CSMA_PARAMETERS:
        csma_params = (phy_csma_params_t*)data_ptr;
        if (csma_params->backoff_time == 0) {
            rf_cca_timer_stop();
            if (rf_read_register_with_status(TX_FIFO_STATUS, NULL)) {
                rf_send_command(S2LP_CMD_SABORT);
                rf_poll_state_change(S2LP_STATE_READY);
                rf_send_command(S2LP_CMD_FLUSHTXFIFO);
                rf_poll_state_change(S2LP_STATE_READY);
            }
            if (rf_state == RF_TX_STARTED) {
                rf_state = RF_IDLE;
                rf_receive(rf_rx_channel);
            }
            tx_time = 0;
        } else {
            tx_time = csma_params->backoff_time;
            cca_enabled = csma_params->cca_enabled;
        }
        break;
    case PHY_EXTENSION_READ_TX_FINNISH_TIME:
        timer_value = (uint32_t*)data_ptr;
        *timer_value = tx_finnish_time;
        break;

    case PHY_EXTENSION_GET_SYMBOLS_PER_SECOND:
        timer_value = (uint32_t*)data_ptr;
        *timer_value = symbols_in_seconds;
        break;

    default:
        break;
    }

    return retval;
}


static int8_t rf_interface_state_control(phy_interface_state_e new_state, uint8_t rf_channel)
{
    int8_t ret_val = 0;
    switch (new_state)
    {
    /*Reset PHY driver and set to idle*/
    case PHY_INTERFACE_RESET:
        break;
        /*Disable PHY Interface driver*/
    case PHY_INTERFACE_DOWN:
        break;
        /*Enable PHY Interface driver*/
    case PHY_INTERFACE_UP:
        rf_receive(rf_channel);
        break;
        /*Enable wireless interface ED scan mode*/
    case PHY_INTERFACE_RX_ENERGY_STATE:
        break;
        /*Enable Sniffer state*/
    case PHY_INTERFACE_SNIFFER_STATE:
        break;
    }
    return ret_val;
}

static void rf_tx_sent_handler(void)
{
    rf_backup_timer_stop();
    rf_disable_interrupt(TX_DATA_SENT);
    if (rf_state != RF_TX_ACK) {
        tx_finnish_time = rf_get_timestamp();
        TEST_TX_DONE
        rf_state = RF_IDLE;
        rf_receive(rf_rx_channel);
        if(device_driver.phy_tx_done_cb){
            device_driver.phy_tx_done_cb(rf_radio_driver_id, mac_tx_handle, PHY_LINK_TX_SUCCESS, 0, 0);
        }
    } else {
        TEST_ACK_TX_DONE
        rf_receive(rf_rx_channel);
    }
}

static void rf_tx_threshold_handler(void)
{
    rf_disable_interrupt(TX_FIFO_ALMOST_EMPTY);
    // TODO check the FIFO threshold. By default, threshold is half of the FIFO size
    uint8_t written_length = rf_write_tx_fifo(tx_data_ptr, tx_data_length, FIFO_SIZE/2);
    if (written_length < tx_data_length) {
        tx_data_ptr += written_length;
        tx_data_length -= written_length;
        rf_enable_interrupt(TX_FIFO_ALMOST_EMPTY);
    }
}

static void rf_start_tx(void)
{
    rf_disable_all_interrupts();
    // More TX data to be written in FIFO when TX threshold interrupt occurs
    if (tx_data_ptr) {
        rf_enable_interrupt(TX_FIFO_ALMOST_EMPTY);
    }
    rf_enable_interrupt(TX_DATA_SENT);
    rf_enable_interrupt(TX_FIFO_UNF_OVF);
    rf_state_change(S2LP_STATE_READY, false);
    rf_state_change(S2LP_STATE_LOCK, true);
    rf_state_change(S2LP_STATE_TX, false);
    rf_backup_timer_start(MAX_PACKET_SENDING_TIME);
}

static void rf_cca_timer_interrupt(void)
{
    if (device_driver.phy_tx_done_cb(rf_radio_driver_id, mac_tx_handle, PHY_LINK_CCA_PREPARE, 0, 0) != 0) {
        if (rf_read_register_with_status(TX_FIFO_STATUS, NULL)) {
            rf_send_command(S2LP_CMD_FLUSHTXFIFO);
        }
        rf_state = RF_IDLE;
        return;
    }
    if ((cca_enabled == true) && (rf_read_register_with_status(LINK_QUALIF1, NULL) & CARRIER_SENSE || (rf_state != RF_CSMA_STARTED && rf_state != RF_IDLE))) {
        if (rf_state == RF_CSMA_STARTED) {
            rf_state = RF_IDLE;
        }
        if (rf_read_register_with_status(TX_FIFO_STATUS, NULL)) {
            rf_send_command(S2LP_CMD_FLUSHTXFIFO);
        }
        tx_finnish_time = rf_get_timestamp();
        if (device_driver.phy_tx_done_cb) {
            device_driver.phy_tx_done_cb(rf_radio_driver_id, mac_tx_handle, PHY_LINK_CCA_FAIL, 0, 0);
        }
    } else {
        rf_start_tx();
        rf_state = RF_TX_STARTED;
        TEST_TX_STARTED
    }
}

static void rf_cca_timer_stop(void)
{
    rf->cca_timer.detach();
}

static void rf_cca_timer_start(uint32_t slots)
{
#ifdef MBED_CONF_RTOS_PRESENT
    rf->cca_timer.attach_us(rf_cca_timer_signal, slots);
#else //MBED_CONF_RTOS_PRESENT
    rf->cca_timer.attach_us(rf_cca_timer_interrupt, slots);
#endif //MBED_CONF_RTOS_PRESENT
}

static void rf_backup_timer_interrupt(void)
{
    tx_finnish_time = rf_get_timestamp();
    if (rf_state == RF_TX_STARTED) {
        if (device_driver.phy_tx_done_cb) {
            device_driver.phy_tx_done_cb(rf_radio_driver_id, mac_tx_handle, PHY_LINK_TX_SUCCESS, 0, 0);
        }
    }
    if (rf_read_register_with_status(TX_FIFO_STATUS, NULL)) {
        rf_send_command(S2LP_CMD_FLUSHTXFIFO);
    }
    TEST_TX_DONE
    TEST_RX_DONE
    rf_state = RF_IDLE;
    rf_receive(rf_rx_channel);
}

static void rf_backup_timer_stop(void)
{
    rf->backup_timer.detach();
}

static void rf_backup_timer_start(uint32_t slots)
{
#ifdef MBED_CONF_RTOS_PRESENT
    rf->backup_timer.attach_us(rf_backup_timer_signal, slots);
#else //MBED_CONF_RTOS_PRESENT
    rf->backup_timer.attach_us(rf_backup_timer_interrupt, slots);
#endif //MBED_CONF_RTOS_PRESENT
}

static int8_t rf_start_cca(uint8_t *data_ptr, uint16_t data_length, uint8_t tx_handle, data_protocol_e data_protocol)
{
    rf_lock();
    if (rf_state != RF_IDLE) {
        rf_unlock();
        return -1;
    }
    rf_state = RF_CSMA_STARTED;
    uint8_t written_length = rf_write_tx_fifo(data_ptr, data_length, FIFO_SIZE);
    if (written_length < data_length) {
        tx_data_ptr = data_ptr + written_length;
        tx_data_length = data_length - written_length;
    } else {
        tx_data_ptr = NULL;
    }
    // If Ack is requested, store the MAC sequence. This will be compared with received Ack.
    uint8_t version = ((*(data_ptr + 1) & VERSION_FIELD_MASK) >> SHIFT_VERSION_FIELD);
    if ((version != MAC_FRAME_VERSION_2) && (*data_ptr & FC_AR)) {
        tx_sequence = *(data_ptr + 2);
    }
    // TODO: Define this better
    rf_write_packet_length(data_length+4);
    mac_tx_handle = tx_handle;
    if (tx_time) {
        uint32_t backoff_time = tx_time - rf_get_timestamp();
        // Max. time to TX can be 65ms, otherwise time has passed already -> send immediately
        if (backoff_time <= 65000) {
            rf_cca_timer_start(backoff_time);
            rf_unlock();
            return 0;
        }
    }
    rf_cca_timer_interrupt();
    rf_unlock();
    return 0;
}

static void rf_send_ack(uint8_t seq)
{
    // If the reception started during CCA process, the TX FIFO may already contain a data packet
    if (rf_read_register_with_status(TX_FIFO_STATUS, NULL)) {
        rf_send_command(S2LP_CMD_SABORT);
        rf_poll_state_change(S2LP_STATE_READY);
        rf_send_command(S2LP_CMD_FLUSHTXFIFO);
        rf_poll_state_change(S2LP_STATE_READY);
    }
    rf_state = RF_TX_ACK;
    uint8_t ack_frame[3] = {MAC_TYPE_ACK, 0, seq};
    rf_write_tx_fifo(ack_frame, sizeof(ack_frame), FIFO_SIZE);
    rf_write_packet_length(sizeof(ack_frame)+4);
    tx_data_ptr = NULL;
    rf_start_tx();
    TEST_ACK_TX_STARTED
    rf_backup_timer_start(ACK_SENDING_TIME);
}

static void rf_handle_ack(uint8_t seq_number, uint8_t pending)
{
    phy_link_tx_status_e phy_status;
    if (tx_sequence == (uint16_t)seq_number) {
        tx_finnish_time = rf_get_timestamp();
        if (pending) {
            phy_status = PHY_LINK_TX_DONE_PENDING;
        } else {
            phy_status = PHY_LINK_TX_DONE;
        }
        // No CCA attempts done, just waited Ack
        device_driver.phy_tx_done_cb(rf_radio_driver_id, mac_tx_handle, phy_status, 0, 0);
        // Clear TX sequence when Ack is received to avoid duplicate Acks
        tx_sequence = 0xffff;
    }
}

static void rf_rx_ready_handler(void)
{
    rf_backup_timer_stop();
    TEST_RX_DONE
    rx_data_length += rf_read_rx_fifo(&rx_buffer[rx_data_length], rf_read_register_with_status(RX_FIFO_STATUS, NULL));
    uint8_t version = ((rx_buffer[1] & VERSION_FIELD_MASK) >> SHIFT_VERSION_FIELD);
    if (((rx_buffer[0] & MAC_FRAME_TYPE_MASK) == MAC_TYPE_ACK) && (version < MAC_FRAME_VERSION_2)) {
        rf_handle_ack(rx_buffer[2], rx_buffer[0] & MAC_DATA_PENDING);
    } else if (rf_rx_filter(rx_buffer, s2lp_MAC, s2lp_short_address, s2lp_PAN_ID)) {
        rf_state = RF_IDLE;
        int8_t rssi = (rf_read_register_with_status(RSSI_LEVEL, NULL) - RSSI_OFFSET);
        if( device_driver.phy_rx_cb ){
            device_driver.phy_rx_cb(rx_buffer, rx_data_length, 0xf0, rssi, rf_radio_driver_id);
        }
        // Check Ack request
        if ((version != MAC_FRAME_VERSION_2) && (rx_buffer[0] & FC_AR)) {
            rf_send_ack(rx_buffer[2]);
        }
    }
    if ((rf_state != RF_TX_ACK) && (rf_state != RF_CSMA_STARTED)) {
        rf_receive(rf_rx_channel);
    }
}

static void rf_rx_threshold_handler(void)
{
    rx_data_length += rf_read_rx_fifo(&rx_buffer[rx_data_length], rf_read_register_with_status(RX_FIFO_STATUS, NULL));
}

static void rf_sync_detected_handler(void)
{
    rx_time = rf_get_timestamp();
    rf_state = RF_RX_STARTED;
    TEST_RX_STARTED
    rf_disable_interrupt(SYNC_WORD);
    rf_backup_timer_start(MAX_PACKET_SENDING_TIME);
}

static void rf_receive(uint8_t rx_channel)
{
    if (rf_state == RF_TX_STARTED) {
        return;
    }
    rf_lock();
    rf_disable_all_interrupts();
    rf_state_change(S2LP_STATE_READY, false);
    rf_send_command(S2LP_CMD_FLUSHRXFIFO);
    rf_poll_state_change(S2LP_STATE_READY);
    if (rx_channel != rf_rx_channel) {
        rf_write_register(CHNUM, rx_channel*rf_channel_multiplier);
        rf_rx_channel = rf_new_channel = rx_channel;
    }
    rf_state_change(S2LP_STATE_LOCK, false);
    rf_state_change(S2LP_STATE_RX, false);
    rf_enable_interrupt(SYNC_WORD);
    rf_enable_interrupt(RX_FIFO_ALMOST_FULL);
    rf_enable_interrupt(RX_DATA_READY);
    rf_enable_interrupt(RX_FIFO_UNF_OVF);
    rx_data_length = 0;
    rf_state = RF_IDLE;
    rf_unlock();
}

#ifdef MBED_CONF_RTOS_PRESENT
static void rf_interrupt_handler(void)
{
    rf->irq_thread.signal_set(SIG_RADIO);
}

void RFPins::rf_irq_task(void)
{
    for (;;) {
        osEvent event = irq_thread.signal_wait(0);
        if (event.status != osEventSignal) {
            continue;
        }
        rf_lock();
        if (event.value.signals & SIG_RADIO) {
            rf_irq_task_process_irq();
        }
        if (event.value.signals & SIG_TIMER_CCA) {
            rf_cca_timer_interrupt();
        }
        if (event.value.signals & SIG_TIMER_BACKUP) {
            rf_backup_timer_interrupt();
        }
        rf_unlock();
    }
}

static void rf_irq_task_process_irq(void)
#else //MBED_CONF_RTOS_PRESENT
static void rf_interrupt_handler(void)
#endif //MBED_CONF_RTOS_PRESENT
{
    rf_lock();
    uint32_t irq_status = read_irq_status();
    if (rf_state == RF_TX_STARTED || rf_state == RF_TX_ACK) {
        if ((irq_status & (1 << TX_DATA_SENT)) && (enabled_interrupts & (1 << TX_DATA_SENT))) {
            rf_tx_sent_handler();
        }
    }
    if (rf_state == RF_TX_STARTED) {
        if ((irq_status & (1 << TX_FIFO_ALMOST_EMPTY)) && (enabled_interrupts & (1 << TX_FIFO_ALMOST_EMPTY))) {
            rf_tx_threshold_handler();
        }
    }
    if ((irq_status & (1 << TX_FIFO_UNF_OVF)) && (enabled_interrupts & (1 << TX_FIFO_UNF_OVF))) {
        tx_finnish_time = rf_get_timestamp();
        TEST_TX_DONE
        device_driver.phy_tx_done_cb(rf_radio_driver_id, mac_tx_handle, PHY_LINK_TX_FAIL, 1, 0);
        rf_send_command(S2LP_CMD_SABORT);
        rf_poll_state_change(S2LP_STATE_READY);
        rf_send_command(S2LP_CMD_FLUSHTXFIFO);
        rf_poll_state_change(S2LP_STATE_READY);
        rf_state = RF_IDLE;
        rf_receive(rf_rx_channel);
    }
    if (rf_state == RF_IDLE || rf_state == RF_TX_STARTED) {
        if ((irq_status & (1 << SYNC_WORD)) && (enabled_interrupts & (1 << SYNC_WORD))) {
            rf_sync_detected_handler();
        }
    } else if (rf_state == RF_RX_STARTED) {
        if ((irq_status & (1 << RX_DATA_READY)) && (enabled_interrupts & (1 << RX_DATA_READY))) {
            if (!(irq_status & (1 << CRC_ERROR))) {
                rf_rx_ready_handler();
            } else {
                rf_state = RF_IDLE;
                // In case the channel change was called during reception, driver is responsible to change the channel if CRC failed.
                rf_receive(rf_new_channel);
            }
        }
        if ((irq_status & (1 << RX_FIFO_ALMOST_FULL)) && (enabled_interrupts & (1 << RX_FIFO_ALMOST_FULL))) {
            rf_rx_threshold_handler();
        }
        if ((irq_status & (1 << RX_DATA_DISCARDED)) && (enabled_interrupts & (1 << RX_DATA_DISCARDED))) {

        }
        if ((irq_status & (1 << CRC_ERROR)) && (enabled_interrupts & (1 << CRC_ERROR))) {

        }
    }
    if ((irq_status & (1 << RX_FIFO_UNF_OVF)) && (enabled_interrupts & (1 << RX_FIFO_UNF_OVF))) {
        TEST_RX_DONE
        rf_send_command(S2LP_CMD_SABORT);
        rf_poll_state_change(S2LP_STATE_READY);
        rf_send_command(S2LP_CMD_FLUSHRXFIFO);
        rf_poll_state_change(S2LP_STATE_READY);
        rf_state = RF_IDLE;
        rf_receive(rf_rx_channel);
    }
    rf_unlock();
}

static void rf_reset(void)
{
    // Shutdown
    rf->SDN = 1;
    wait_ms(10);
    // Wake up
    rf->SDN = 0;
    // Wait until GPIO0 (RESETN) goes high
    while (rf->GPIO0 == 0);
}

static void rf_init(void)
{
#ifdef TEST_GPIOS_ENABLED
    fhss_bc_switch = test1_toggle;
    fhss_uc_switch = test2_toggle;
#endif //TEST_GPIOS_ENABLED
    rf_reset();
    rf->spi.frequency(10000000);
    CS_RELEASE();
    if (PARTNUM != rf_read_register_with_status(DEVICE_INFO1, NULL)) {
        tr_err("Invalid part number: %x", rf_read_register_with_status(DEVICE_INFO1, NULL));
    }
    if (VERSION != rf_read_register_with_status(DEVICE_INFO0, NULL)) {
        tr_err("Invalid version: %x", rf_read_register_with_status(DEVICE_INFO0, NULL));
    }
    rf_init_registers();
    rf_enable_gpio_interrupt();
    rf_calculate_symbols_in_seconds(phy_subghz.datarate, phy_subghz.modulation);
    rf->tx_timer.start();
    rf_print_registers();
}

static int8_t rf_device_register(const uint8_t *mac_addr)
{
    rf_init();
    /*Set pointer to MAC address*/
    device_driver.PHY_MAC = (uint8_t *)mac_addr;
    device_driver.driver_description = (char*)"S2LP_MAC";
    device_driver.link_type = PHY_LINK_15_4_SUBGHZ_TYPE;
    device_driver.phy_channel_pages = phy_channel_pages;
    device_driver.phy_MTU = RF_MTU;
    /*No header in PHY*/
    device_driver.phy_header_length = 0;
    /*No tail in PHY*/
    device_driver.phy_tail_length = 0;
    /*Set address write function*/
    device_driver.address_write = &rf_address_write;
    /*Set RF extension function*/
    device_driver.extension = &rf_extension;
    /*Set RF state control function*/
    device_driver.state_control = &rf_interface_state_control;
    /*Set transmit function*/
    device_driver.tx = &rf_start_cca;
    /*NULLIFY rx and tx_done callbacks*/
    device_driver.phy_rx_cb = NULL;
    device_driver.phy_tx_done_cb = NULL;
    /*Register device driver*/
    rf_radio_driver_id = arm_net_phy_register(&device_driver);
    return rf_radio_driver_id;
}

static void rf_device_unregister()
{
    if (rf_radio_driver_id >= 0) {
        arm_net_phy_unregister(rf_radio_driver_id);
        rf_radio_driver_id = -1;
    }
}

void NanostackRfPhys2lp::get_mac_address(uint8_t *mac)
{

}

void NanostackRfPhys2lp::set_mac_address(uint8_t *mac)
{

}

int8_t NanostackRfPhys2lp::rf_register()
{
    if (NULL == _rf) {
        return -1;
    }
    rf_lock();
    if (rf != NULL) {
        rf_unlock();
        error("Multiple registrations of NanostackRfPhyAtmel not supported");
        return -1;
    }
    rf = _rf;
    int8_t radio_id = rf_device_register(_mac_addr);
    if (radio_id < 0) {
        rf = NULL;
    }
    rf_unlock();
    return radio_id;
}

void NanostackRfPhys2lp::rf_unregister()
{
    rf_lock();
    if (NULL == rf) {
        rf_unlock();
        return;
    }
    rf_device_unregister();
    rf = NULL;
    rf_unlock();
}

NanostackRfPhys2lp::NanostackRfPhys2lp(PinName spi_sdi, PinName spi_sdo, PinName spi_sclk, PinName spi_cs, PinName spi_sdn,
#ifdef TEST_GPIOS_ENABLED
        PinName spi_test1, PinName spi_test2, PinName spi_test3, PinName spi_test4, PinName spi_test5,
#endif //TEST_GPIOS_ENABLED
        PinName spi_gpio0, PinName spi_gpio1, PinName spi_gpio2, PinName spi_gpio3)
: _mac_addr(), _rf(NULL), _mac_set(false),
  _spi_sdi(spi_sdi), _spi_sdo(spi_sdo), _spi_sclk(spi_sclk), _spi_cs(spi_cs), _spi_sdn(spi_sdn),
#ifdef TEST_GPIOS_ENABLED
  _spi_test1(spi_test1), _spi_test2(spi_test2), _spi_test3(spi_test3), _spi_test4(spi_test4), _spi_test5(spi_test5),
#endif //TEST_GPIOS_ENABLED
  _spi_gpio0(spi_gpio0), _spi_gpio1(spi_gpio1), _spi_gpio2(spi_gpio2), _spi_gpio3(spi_gpio3)
{
    _rf = new RFPins(_spi_sdi, _spi_sdo, _spi_sclk, _spi_cs, _spi_sdn,
#ifdef TEST_GPIOS_ENABLED
            _spi_test1, _spi_test2, _spi_test3, _spi_test4, _spi_test5,
#endif //TEST_GPIOS_ENABLED
            _spi_gpio0, _spi_gpio1, _spi_gpio2, _spi_gpio3);
}

NanostackRfPhys2lp::~NanostackRfPhys2lp()
{
    delete _rf;
}

static bool rf_panid_filter_common(uint8_t *panid_start, uint8_t *pan_id, uint8_t frame_type)
{
    // PHY driver shouldn't drop received Beacon frames as they might be used by load balancing
    if (frame_type == MAC_FRAME_BEACON) {
        return true;
    }
    bool retval = true;
    uint8_t cmp_table[2] = {0xff, 0xff};
    if (!(pan_id[0] == 0xff && pan_id[1] == 0xff)) {
        if (memcmp((uint8_t *)panid_start, (uint8_t *) cmp_table, 2)) {
            retval = false;
        }
        if (!retval) {
            for (uint8_t i = 0; i < 2; i++) {
                cmp_table[1 - i] = panid_start[i];
            }
            if (!memcmp(pan_id, cmp_table, 2)) {
                retval = true;
            }
        }
    }
    return retval;
}

static bool rf_panid_v2_filter(uint8_t *ptr, uint8_t *pan_id, uint8_t dst_mode, uint8_t src_mode, uint8_t seq_compressed, uint8_t panid_compressed, uint8_t frame_type)
{
    if ((dst_mode == FC_DST_ADDR_NONE) && (frame_type == MAC_TYPE_DATA || frame_type == MAC_TYPE_COMMAND)) {
        return true;
    }
    if ((dst_mode == FC_DST_64_BITS) && (src_mode == FC_SRC_64_BITS) && panid_compressed) {
        return true;
    }
    if (seq_compressed) {
        ptr--;
    }
    return rf_panid_filter_common(ptr, pan_id, frame_type);
}

static bool rf_panid_v1_v0_filter(uint8_t *ptr, uint8_t *pan_id, uint8_t frame_type)
{
    return rf_panid_filter_common(ptr, pan_id, frame_type);
}

static bool rf_addr_filter_common(uint8_t *ptr, uint8_t addr_mode, uint8_t *mac_64bit_addr, uint8_t *mac_16bit_addr)
{
    uint8_t cmp_table[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    bool retval = true;
    switch (addr_mode) {
        case FC_DST_16_BITS:
            if (memcmp((uint8_t *)ptr, (uint8_t *) cmp_table, 2)) {
                retval = false;
            }
            if (!retval) {
                for (uint8_t i = 0; i < 2; i++) {
                    cmp_table[1 - i] = ptr[i];
                }

                if (!memcmp((uint8_t *)mac_16bit_addr, (uint8_t *) cmp_table, 2)) {
                    retval = true;
                }
            }
            break;
        case FC_DST_64_BITS:
            if (memcmp((uint8_t *)ptr, (uint8_t *) cmp_table, 8)) {
                retval = false;
            }
            if (!retval) {
                for (uint8_t i = 0; i < 8; i++) {
                    cmp_table[7 - i] = ptr[i];
                }

                if (!memcmp((uint8_t *)mac_64bit_addr, (uint8_t *) cmp_table, 8)) {
                    retval = true;
                }
            }
            break;
        case FC_DST_ADDR_NONE:
            retval = true;
            break;
        default:
            retval = false;
            break;
    }
    return retval;
}

static bool rf_addr_v2_filter(uint8_t *ptr, uint8_t *mac_64bit_addr, uint8_t *mac_16bit_addr, uint8_t dst_mode, uint8_t seq_compressed, uint8_t panid_compressed)
{
    if (seq_compressed) {
        ptr--;
    }
    if (panid_compressed) {
        ptr -= 2;
    }
    return rf_addr_filter_common(ptr, dst_mode, mac_64bit_addr, mac_16bit_addr);
}

static bool rf_addr_v1_v0_filter(uint8_t *ptr, uint8_t *mac_64bit_addr, uint8_t *mac_16bit_addr, uint8_t dst_mode)
{
    return rf_addr_filter_common(ptr, dst_mode, mac_64bit_addr, mac_16bit_addr);
}

static bool rf_rx_filter(uint8_t *mac_header, uint8_t *mac_64bit_addr, uint8_t *mac_16bit_addr, uint8_t *pan_id)
{
    uint8_t dst_mode = (mac_header[1] & FC_DST_MODE);
    uint8_t src_mode = (mac_header[1] & FC_SRC_MODE);
    uint8_t seq_compressed = ((mac_header[1] & FC_SEQUENCE_COMPRESSION) >> SHIFT_SEQ_COMP_FIELD);
    uint8_t panid_compressed = ((mac_header[0] & FC_PAN_ID_COMPRESSION) >> SHIFT_PANID_COMP_FIELD);
    uint8_t frame_type = mac_header[0] & MAC_FRAME_TYPE_MASK;
    uint8_t version = ((mac_header[1] & VERSION_FIELD_MASK) >> SHIFT_VERSION_FIELD);
    if (version == MAC_FRAME_VERSION_2) {
        if (!rf_panid_v2_filter(mac_header + OFFSET_DST_PAN_ID, pan_id, dst_mode, src_mode, seq_compressed, panid_compressed, frame_type)) {
            return false;
        }
        if (!rf_addr_v2_filter(mac_header + OFFSET_DST_ADDR, mac_64bit_addr, mac_16bit_addr, dst_mode, seq_compressed, panid_compressed)) {
            return false;
        }
    } else {
        if (!rf_panid_v1_v0_filter(mac_header + OFFSET_DST_PAN_ID, pan_id, frame_type)) {
            return false;
        }
        if (!rf_addr_v1_v0_filter(mac_header + OFFSET_DST_ADDR, mac_64bit_addr, mac_16bit_addr, dst_mode)) {
            return false;
        }
    }
    return true;
}

static void rf_print_registers(void)
{
    tr_debug("GPIO0_CONF: %x", rf_read_register_with_status(GPIO0_CONF, NULL));
    tr_debug("GPIO1_CONF: %x", rf_read_register_with_status(GPIO1_CONF, NULL));
    tr_debug("GPIO2_CONF: %x", rf_read_register_with_status(GPIO2_CONF, NULL));
    tr_debug("GPIO3_CONF: %x", rf_read_register_with_status(GPIO3_CONF, NULL));
    tr_debug("SYNT3: %x", rf_read_register_with_status(SYNT3, NULL));
    tr_debug("SYNT2: %x", rf_read_register_with_status(SYNT2, NULL));
    tr_debug("SYNT1: %x", rf_read_register_with_status(SYNT1, NULL));
    tr_debug("SYNT0: %x", rf_read_register_with_status(SYNT0, NULL));
    tr_debug("IF_OFFSET_ANA: %x", rf_read_register_with_status(IF_OFFSET_ANA, NULL));
    tr_debug("IF_OFFSET_DIG: %x", rf_read_register_with_status(IF_OFFSET_DIG, NULL));
    tr_debug("CHSPACE: %x", rf_read_register_with_status(CHSPACE, NULL));
    tr_debug("CHNUM: %x", rf_read_register_with_status(CHNUM, NULL));
    tr_debug("MOD4: %x", rf_read_register_with_status(MOD4, NULL));
    tr_debug("MOD3: %x", rf_read_register_with_status(MOD3, NULL));
    tr_debug("MOD2: %x", rf_read_register_with_status(MOD2, NULL));
    tr_debug("MOD1: %x", rf_read_register_with_status(MOD1, NULL));
    tr_debug("MOD0: %x", rf_read_register_with_status(MOD0, NULL));
    tr_debug("CHFLT: %x", rf_read_register_with_status(CHFLT, NULL));
    tr_debug("AFC2: %x", rf_read_register_with_status(AFC2, NULL));
    tr_debug("AFC1: %x", rf_read_register_with_status(AFC1, NULL));
    tr_debug("AFC0: %x", rf_read_register_with_status(AFC0, NULL));
    tr_debug("RSSI_FLT: %x", rf_read_register_with_status(RSSI_FLT, NULL));
    tr_debug("RSSI_TH: %x", rf_read_register_with_status(RSSI_TH, NULL));
    tr_debug("AGCCTRL4: %x", rf_read_register_with_status(AGCCTRL4, NULL));
    tr_debug("AGCCTRL3: %x", rf_read_register_with_status(AGCCTRL3, NULL));
    tr_debug("AGCCTRL2: %x", rf_read_register_with_status(AGCCTRL2, NULL));
    tr_debug("AGCCTRL1: %x", rf_read_register_with_status(AGCCTRL1, NULL));
    tr_debug("AGCCTRL0: %x", rf_read_register_with_status(AGCCTRL0, NULL));
    tr_debug("ANT_SELECT_CONF: %x", rf_read_register_with_status(ANT_SELECT_CONF, NULL));
    tr_debug("CLOCKREC2: %x", rf_read_register_with_status(CLOCKREC2, NULL));
    tr_debug("CLOCKREC1: %x", rf_read_register_with_status(CLOCKREC1, NULL));
    tr_debug("PCKTCTRL6: %x", rf_read_register_with_status(PCKTCTRL6, NULL));
    tr_debug("PCKTCTRL5: %x", rf_read_register_with_status(PCKTCTRL5, NULL));
    tr_debug("PCKTCTRL4: %x", rf_read_register_with_status(PCKTCTRL4, NULL));
    tr_debug("PCKTCTRL3: %x", rf_read_register_with_status(PCKTCTRL3, NULL));
    tr_debug("PCKTCTRL2: %x", rf_read_register_with_status(PCKTCTRL2, NULL));
    tr_debug("PCKTCTRL1: %x", rf_read_register_with_status(PCKTCTRL1, NULL));
    tr_debug("PCKTLEN1: %x", rf_read_register_with_status(PCKTLEN1, NULL));
    tr_debug("PCKTLEN0: %x", rf_read_register_with_status(PCKTLEN0, NULL));
    tr_debug("SYNC3: %x", rf_read_register_with_status(SYNC3, NULL));
    tr_debug("SYNC2: %x", rf_read_register_with_status(SYNC2, NULL));
    tr_debug("SYNC1: %x", rf_read_register_with_status(SYNC1, NULL));
    tr_debug("SYNC0: %x", rf_read_register_with_status(SYNC0, NULL));
    tr_debug("QI: %x", rf_read_register_with_status(QI, NULL));
    tr_debug("PCKT_PSTMBL: %x", rf_read_register_with_status(PCKT_PSTMBL, NULL));
    tr_debug("PROTOCOL2: %x", rf_read_register_with_status(PROTOCOL2, NULL));
    tr_debug("PROTOCOL1: %x", rf_read_register_with_status(PROTOCOL1, NULL));
    tr_debug("PROTOCOL0: %x", rf_read_register_with_status(PROTOCOL0, NULL));
    tr_debug("FIFO_CONFIG3: %x", rf_read_register_with_status(FIFO_CONFIG3, NULL));
    tr_debug("FIFO_CONFIG2: %x", rf_read_register_with_status(FIFO_CONFIG2, NULL));
    tr_debug("FIFO_CONFIG1: %x", rf_read_register_with_status(FIFO_CONFIG1, NULL));
    tr_debug("FIFO_CONFIG0: %x", rf_read_register_with_status(FIFO_CONFIG0, NULL));
    tr_debug("PCKT_FLT_OPTIONS: %x", rf_read_register_with_status(PCKT_FLT_OPTIONS, NULL));
    tr_debug("PCKT_FLT_GOALS4: %x", rf_read_register_with_status(PCKT_FLT_GOALS4, NULL));
    tr_debug("PCKT_FLT_GOALS3: %x", rf_read_register_with_status(PCKT_FLT_GOALS3, NULL));
    tr_debug("PCKT_FLT_GOALS2: %x", rf_read_register_with_status(PCKT_FLT_GOALS2, NULL));
    tr_debug("PCKT_FLT_GOALS1: %x", rf_read_register_with_status(PCKT_FLT_GOALS1, NULL));
    tr_debug("PCKT_FLT_GOALS0: %x", rf_read_register_with_status(PCKT_FLT_GOALS0, NULL));
    tr_debug("TIMERS5: %x", rf_read_register_with_status(TIMERS5, NULL));
    tr_debug("TIMERS4: %x", rf_read_register_with_status(TIMERS4, NULL));
    tr_debug("TIMERS3: %x", rf_read_register_with_status(TIMERS3, NULL));
    tr_debug("TIMERS2: %x", rf_read_register_with_status(TIMERS2, NULL));
    tr_debug("TIMERS1: %x", rf_read_register_with_status(TIMERS1, NULL));
    tr_debug("TIMERS0: %x", rf_read_register_with_status(TIMERS0, NULL));
    tr_debug("CSMA_CONF3: %x", rf_read_register_with_status(CSMA_CONF3, NULL));
    tr_debug("CSMA_CONF2: %x", rf_read_register_with_status(CSMA_CONF2, NULL));
    tr_debug("CSMA_CONF1: %x", rf_read_register_with_status(CSMA_CONF1, NULL));
    tr_debug("CSMA_CONF0: %x", rf_read_register_with_status(CSMA_CONF0, NULL));
    tr_debug("IRQ_MASK3: %x", rf_read_register_with_status(IRQ_MASK3, NULL));
    tr_debug("IRQ_MASK2: %x", rf_read_register_with_status(IRQ_MASK2, NULL));
    tr_debug("IRQ_MASK1: %x", rf_read_register_with_status(IRQ_MASK1, NULL));
    tr_debug("IRQ_MASK0: %x", rf_read_register_with_status(IRQ_MASK0, NULL));
    tr_debug("FAST_RX_TIMER: %x", rf_read_register_with_status(FAST_RX_TIMER, NULL));
    tr_debug("PA_POWER8: %x", rf_read_register_with_status(PA_POWER8, NULL));
    tr_debug("PA_POWER7: %x", rf_read_register_with_status(PA_POWER7, NULL));
    tr_debug("PA_POWER6: %x", rf_read_register_with_status(PA_POWER6, NULL));
    tr_debug("PA_POWER5: %x", rf_read_register_with_status(PA_POWER5, NULL));
    tr_debug("PA_POWER4: %x", rf_read_register_with_status(PA_POWER4, NULL));
    tr_debug("PA_POWER3: %x", rf_read_register_with_status(PA_POWER3, NULL));
    tr_debug("PA_POWER2: %x", rf_read_register_with_status(PA_POWER2, NULL));
    tr_debug("PA_POWER1: %x", rf_read_register_with_status(PA_POWER1, NULL));
    tr_debug("PA_POWER0: %x", rf_read_register_with_status(PA_POWER0, NULL));
    tr_debug("PA_CONFIG1: %x", rf_read_register_with_status(PA_CONFIG1, NULL));
    tr_debug("PA_CONFIG0: %x", rf_read_register_with_status(PA_CONFIG0, NULL));
    tr_debug("SYNTH_CONFIG2: %x", rf_read_register_with_status(SYNTH_CONFIG2, NULL));
    tr_debug("VCO_CONFIG: %x", rf_read_register_with_status(VCO_CONFIG, NULL));
    tr_debug("VCO_CALIBR_IN2: %x", rf_read_register_with_status(VCO_CALIBR_IN2, NULL));
    tr_debug("VCO_CALIBR_IN1: %x", rf_read_register_with_status(VCO_CALIBR_IN1, NULL));
    tr_debug("VCO_CALIBR_IN0: %x", rf_read_register_with_status(VCO_CALIBR_IN0, NULL));
    tr_debug("XO_RCO_CONF1: %x", rf_read_register_with_status(XO_RCO_CONF1, NULL));
    tr_debug("XO_RCO_CONF0: %x", rf_read_register_with_status(XO_RCO_CONF0, NULL));
    tr_debug("RCO_CALIBR_CONF3: %x", rf_read_register_with_status(RCO_CALIBR_CONF3, NULL));
    tr_debug("RCO_CALIBR_CONF2: %x", rf_read_register_with_status(RCO_CALIBR_CONF2, NULL));
    tr_debug("PM_CONF4: %x", rf_read_register_with_status(PM_CONF4, NULL));
    tr_debug("PM_CONF3: %x", rf_read_register_with_status(PM_CONF3, NULL));
    tr_debug("PM_CONF2: %x", rf_read_register_with_status(PM_CONF2, NULL));
    tr_debug("PM_CONF1: %x", rf_read_register_with_status(PM_CONF1, NULL));
    tr_debug("PM_CONF0: %x", rf_read_register_with_status(PM_CONF0, NULL));
    tr_debug("MC_STATE1: %x", rf_read_register_with_status(MC_STATE1, NULL));
    tr_debug("MC_STATE0: %x", rf_read_register_with_status(MC_STATE0, NULL));
    tr_debug("TX_FIFO_STATUS: %x", rf_read_register_with_status(TX_FIFO_STATUS, NULL));
    tr_debug("RX_FIFO_STATUS: %x", rf_read_register_with_status(RX_FIFO_STATUS, NULL));
    tr_debug("RCO_CALIBR_OUT4: %x", rf_read_register_with_status(RCO_CALIBR_OUT4, NULL));
    tr_debug("RCO_CALIBR_OUT3: %x", rf_read_register_with_status(RCO_CALIBR_OUT3, NULL));
    tr_debug("VCO_CALIBR_OUT1: %x", rf_read_register_with_status(VCO_CALIBR_OUT1, NULL));
    tr_debug("VCO_CALIBR_OUT0: %x", rf_read_register_with_status(VCO_CALIBR_OUT0, NULL));
    tr_debug("TX_PCKT_INFO: %x", rf_read_register_with_status(TX_PCKT_INFO, NULL));
    tr_debug("RX_PCKT_INFO: %x", rf_read_register_with_status(RX_PCKT_INFO, NULL));
    tr_debug("AFC_CORR: %x", rf_read_register_with_status(AFC_CORR, NULL));
    tr_debug("LINK_QUALIF2: %x", rf_read_register_with_status(LINK_QUALIF2, NULL));
    tr_debug("LINK_QUALIF1: %x", rf_read_register_with_status(LINK_QUALIF1, NULL));
    tr_debug("RSSI_LEVEL: %x", rf_read_register_with_status(RSSI_LEVEL, NULL));
    tr_debug("RX_PCKT_LEN1: %x", rf_read_register_with_status(RX_PCKT_LEN1, NULL));
    tr_debug("RX_PCKT_LEN0: %x", rf_read_register_with_status(RX_PCKT_LEN0, NULL));
    tr_debug("CRC_FIELD3: %x", rf_read_register_with_status(CRC_FIELD3, NULL));
    tr_debug("CRC_FIELD2: %x", rf_read_register_with_status(CRC_FIELD2, NULL));
    tr_debug("CRC_FIELD1: %x", rf_read_register_with_status(CRC_FIELD1, NULL));
    tr_debug("CRC_FIELD0: %x", rf_read_register_with_status(CRC_FIELD0, NULL));
    tr_debug("RX_ADDRE_FIELD1: %x", rf_read_register_with_status(RX_ADDRE_FIELD1, NULL));
    tr_debug("RX_ADDRE_FIELD0: %x", rf_read_register_with_status(RX_ADDRE_FIELD0, NULL));
    tr_debug("RSSI_LEVEL_RUN: %x", rf_read_register_with_status(RSSI_LEVEL_RUN, NULL));
    tr_debug("DEVICE_INFO1: %x", rf_read_register_with_status(DEVICE_INFO1, NULL));
    tr_debug("DEVICE_INFO0: %x", rf_read_register_with_status(DEVICE_INFO0, NULL));
    tr_debug("IRQ_STATUS3: %x", rf_read_register_with_status(IRQ_STATUS3, NULL));
    tr_debug("IRQ_STATUS2: %x", rf_read_register_with_status(IRQ_STATUS2, NULL));
    tr_debug("IRQ_STATUS1: %x", rf_read_register_with_status(IRQ_STATUS1, NULL));
    tr_debug("IRQ_STATUS0: %x", rf_read_register_with_status(IRQ_STATUS0, NULL));
}

#if MBED_CONF_S2LP_PROVIDE_DEFAULT
NanostackRfPhy &NanostackRfPhy::get_default_instance()
{
    static NanostackRfPhys2lp rf_phy(S2LP_SPI_SDI, S2LP_SPI_SDO, S2LP_SPI_SCLK, S2LP_SPI_CS, S2LP_SPI_SDN,
#ifdef TEST_GPIOS_ENABLED
        S2LP_SPI_TEST1, S2LP_SPI_TEST2, S2LP_SPI_TEST3, S2LP_SPI_TEST4, S2LP_SPI_TEST5,
#endif //TEST_GPIOS_ENABLED
        S2LP_SPI_GPIO0, S2LP_SPI_GPIO1, S2LP_SPI_GPIO2, S2LP_SPI_GPIO3);
    return rf_phy;
}
#endif // MBED_CONF_S2LP_PROVIDE_DEFAULT
