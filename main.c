/* Copyright (c) 2012 Nordic Semiconductor. All Rights Reserved.
 *
 * The information contained herein is property of Nordic Semiconductor ASA.
 * Terms and conditions of usage are described in detail in NORDIC
 * SEMICONDUCTOR STANDARD SOFTWARE LICENSE AGREEMENT.
 *
 * Licensees are granted free, non-transferable use of the information. NO
 * WARRANTY of ANY KIND is provided. This heading must NOT be removed from
 * the file.
 *
 */

/** @file
 *
 * @defgroup ble_sdk_app_hids_keyboard_main main.c
 * @{
 * @ingroup ble_sdk_app_hids_keyboard
 * @brief HID Keyboard Sample Application main file.
 *
 * This file contains is the source code for a sample application using the HID, Battery and Device
 * Information Services for implementing a simple keyboard functionality.
 * Pressing Button 0 will send text 'hello' to the connected peer. On receiving output report,
 * it toggles the state of LED 2 on the mother board based on whether or not Caps Lock is on.
 * This application uses the @ref app_scheduler.
 *
 * Also it would accept pairing requests from any peer device.
 */

#include <stdint.h>
#include <string.h>
#include "nordic_common.h"
#include "nrf.h"
#include "nrf_assert.h"
#include "app_error.h"
#include "nrf_gpio.h"
#include "ble.h"
#include "ble_hci.h"
#include "ble_srv_common.h"
#include "ble_advertising.h"
#include "ble_advdata.h"
#include "ble_hids.h"
#include "ble_bas.h"
#include "ble_dis.h"
#include "ble_conn_params.h"
#include "bsp.h"
#include "sensorsim.h"
#include "softdevice_handler.h"
#include "app_timer.h"
#include "device_manager.h"
#include "app_button.h"
#include "pstorage.h"
#include "app_trace.h"

#if BUTTONS_NUMBER <2
#error "Not enough resources on board"
#endif

#define IS_SRVC_CHANGED_CHARACT_PRESENT  0                                              /**< Include or not the service_changed characteristic. if not enabled, the server's database cannot be changed for the lifetime of the device*/
#define DEVICE_NAME                      "nRF_Selfie"                                   /**< Name of device. Will be included in the advertising data. */
#define MANUFACTURER_NAME                "NordicSemiconductor"                          /**< Manufacturer. Will be passed to Device Information Service. */

#define APP_TIMER_PRESCALER              0                                              /**< Value of the RTC1 PRESCALER register. */
#define APP_TIMER_MAX_TIMERS             4                                              /**< Maximum number of simultaneously created timers. */
#define APP_TIMER_OP_QUEUE_SIZE          4                                              /**< Size of timer operation queues. */

#define BATTERY_LEVEL_MEAS_INTERVAL      APP_TIMER_TICKS(2000, APP_TIMER_PRESCALER)     /**< Battery level measurement interval (ticks). */
#define MIN_BATTERY_LEVEL                81                                             /**< Minimum simulated battery level. */
#define MAX_BATTERY_LEVEL                100                                            /**< Maximum simulated battery level. */
#define BATTERY_LEVEL_INCREMENT          1                                              /**< Increment between each simulated battery level measurement. */

#define PNP_ID_VENDOR_ID_SOURCE          0x02                                           /**< Vendor ID Source. */
#define PNP_ID_VENDOR_ID                 0x1915                                         /**< Vendor ID. */
#define PNP_ID_PRODUCT_ID                0xEEEE                                         /**< Product ID. */
#define PNP_ID_PRODUCT_VERSION           0x0001                                         /**< Product Version. */

#define APP_ADV_FAST_INTERVAL            0x0028                                         /**< Fast advertising interval (in units of 0.625 ms. This value corresponds to 25 ms.). */
#define APP_ADV_SLOW_INTERVAL            0x0C80                                         /**< Slow advertising interval (in units of 0.625 ms. This value corrsponds to 2 seconds). */
#define APP_ADV_FAST_TIMEOUT             30                                             /**< The duration of the fast advertising period (in seconds). */
#define APP_ADV_SLOW_TIMEOUT             180                                            /**< The duration of the slow advertising period (in seconds). */

/*lint -emacro(524, MIN_CONN_INTERVAL) // Loss of precision */
#define MIN_CONN_INTERVAL                MSEC_TO_UNITS(15, UNIT_1_25_MS)                /**< Minimum connection interval (7.5 ms) */
#define MAX_CONN_INTERVAL                MSEC_TO_UNITS(50, UNIT_1_25_MS)                /**< Maximum connection interval (30 ms). */
#define SLAVE_LATENCY                    10                                              /**< Slave latency. */
#define CONN_SUP_TIMEOUT                 MSEC_TO_UNITS(1500, UNIT_10_MS)                 /**< Connection supervisory timeout (430 ms). */

#define FIRST_CONN_PARAMS_UPDATE_DELAY   APP_TIMER_TICKS(5000, APP_TIMER_PRESCALER)     /**< Time from initiating event (connect or start of notification) to first time sd_ble_gap_conn_param_update is called (5 seconds). */
#define NEXT_CONN_PARAMS_UPDATE_DELAY    APP_TIMER_TICKS(30000, APP_TIMER_PRESCALER)    /**< Time between each call to sd_ble_gap_conn_param_update after the first call (30 seconds). */
#define MAX_CONN_PARAMS_UPDATE_COUNT     3                                              /**< Number of attempts before giving up the connection parameter negotiation. */

#define SEC_PARAM_BOND                   1                                              /**< Perform bonding. */
#define SEC_PARAM_MITM                   0                                              /**< Man In The Middle protection not required. */
#define SEC_PARAM_IO_CAPABILITIES        BLE_GAP_IO_CAPS_NONE                           /**< No I/O capabilities. */
#define SEC_PARAM_OOB                    0                                              /**< Out Of Band data not available. */
#define SEC_PARAM_MIN_KEY_SIZE           7                                              /**< Minimum encryption key size. */
#define SEC_PARAM_MAX_KEY_SIZE           16                                             /**< Maximum encryption key size. */

#define OUTPUT_REPORT_INDEX              0                                              /**< Index of Output Report. */
#define OUTPUT_REPORT_MAX_LEN            1                                              /**< Maximum length of Output Report. */
#define INPUT_REPORT_KEYS_INDEX          0                                              /**< Index of Input Report. */
#define OUTPUT_REPORT_BIT_MASK_CAPS_LOCK 0x02                                           /**< CAPS LOCK bit in Output Report (based on 'LED Page (0x08)' of the Universal Serial Bus HID Usage Tables). */
#define INPUT_REP_REF_ID                 1                                              /**< Id of reference to Keyboard Input Report. */
#define OUTPUT_REP_REF_ID                0                                              /**< Id of reference to Keyboard Output Report. */

#define APP_FEATURE_NOT_SUPPORTED       BLE_GATT_STATUS_ATTERR_APP_BEGIN + 2            /**< Reply when unsupported features are requested. */

#define BASE_USB_HID_SPEC_VERSION        0x0101                                         /**< Version number of base USB HID Specification implemented by this application. */

#define INPUT_REPORT_KEYS_MAX_LEN        8                                              /**< Maximum length of the Input Report characteristic. */

#define DEAD_BEEF                        0xDEADBEEF                                     /**< Value used as error code on stack dump, can be used to identify stack location on stack unwind. */

#define MODIFIER_KEY_POS                 0                                              /**< Position of the modifier byte in the Input Report. */
#define SCAN_CODE_POS                    2                                              /**< This macro indicates the start position of the key scan code in a HID Report. As per the document titled 'Device Class Definition for Human Interface Devices (HID) V1.11, each report shall have one modifier byte followed by a reserved constant byte and then the key scan code. */
#define SHIFT_KEY_CODE                   0x02                                           /**< Key code indicating the press of the Shift Key. */

#define MAX_KEYS_IN_ONE_REPORT           (INPUT_REPORT_KEYS_MAX_LEN - SCAN_CODE_POS)    /**< Maximum number of key presses that can be sent in one Input Report. */

#define INPUT_CCONTROL_KEYS_INDEX		 1
#define INPUT_CC_REP_REF_ID				 2
#define INPUT_CC_REPORT_KEYS_MAX_LEN	 1

#ifdef NFC_ENABLE
/***************************************************************************************************
 * Start of modifications needed for BLE pairing over NFC
 **************************************************************************************************/
#include "nfc_lib.h"
#include "nfc_ble_pair_msg.h"
#include "nfc_ble_pair_msg_config.h"

#define NFC_BLE_JUST_WORKS_PAIRING       0  /**< Just Works pairing over NFC - Temporary Key (TK) always equal to 0 */
#define NFC_BLE_OOB_PAIRING              1  /**< Out-of-Band pairing over NFC - Temporary Key (TK) value can be configured. */
#define NFC_BLE_PAIRING_TYPE             NFC_BLE_JUST_WORKS_PAIRING

static volatile uint8_t                  m_advertising_flag = 0;
static ble_advdata_t                     m_advdata;

#ifdef  HAL_NFC_ENGINEERING_A_FTPAN_WORKAROUND
extern volatile uint8_t                  m_nfc_active;
#endif

#if (NFC_BLE_PAIRING_TYPE == NFC_BLE_OOB_PAIRING)
#define SEC_PARAM_MITM               0  /**< Man In The Middle protection not required. */
#define SEC_PARAM_OOB                1  /**< Out Of Band data available. */
// Hardcoded authentication OOB key
#define OOB_AUTH_KEY                 {                            \
                                        {                         \
                                          0xAA, 0xBB, 0xCC, 0xDD, \
                                          0xEE, 0xFF, 0x99, 0x88, \
                                          0x77, 0x66, 0x55, 0x44, \
                                          0x33, 0x22, 0x11, 0x00  \
                                        }                         \
                                     }

static ble_advdata_tk_value_t        m_oob_auth_key = OOB_AUTH_KEY;
#else
#define SEC_PARAM_MITM               0  /**< Man In The Middle protection not required. */
#define SEC_PARAM_OOB                0  /**< Out Of Band data not available. */
#endif /* NFC_BLE_PAIRING_TYPE */
#endif

typedef enum
{
    BLE_NO_ADV,               /**< No advertising running. */
    BLE_DIRECTED_ADV,         /**< Direct advertising to the latest central. */
    BLE_FAST_ADV_WHITELIST,   /**< Advertising with whitelist. */
    BLE_FAST_ADV,             /**< Fast advertising running. */
    BLE_SLOW_ADV,             /**< Slow advertising running. */
    BLE_SLEEP,                /**< Go to system-off. */
} ble_advertising_mode_t;

static ble_hids_t                        m_hids;                                        /**< Structure used to identify the HID service. */
static ble_bas_t                         m_bas;                                         /**< Structure used to identify the battery service. */
static bool                              m_in_boot_mode = false;                        /**< Current protocol mode. */
static uint16_t                          m_conn_handle = BLE_CONN_HANDLE_INVALID;       /**< Handle of the current connection. */
static app_timer_id_t                    m_battery_timer_id;                            /**< Battery timer. */
static dm_application_instance_t         m_app_handle;                                  /**< Application identifier allocated by device manager. */
static dm_handle_t                       m_bonded_peer_handle;                          /**< Device reference handle to the current bonded central. */

static ble_uuid_t m_adv_uuids[] = {{BLE_UUID_HUMAN_INTERFACE_DEVICE_SERVICE, BLE_UUID_TYPE_BLE}};

typedef enum
{
    RELEASE_KEY                     = 0x00,
    CONSUMER_CTRL_PLAY              = 0x01,
    CONSUMER_CTRL_ALCCC             = 0x02,
    CONSUMER_CTRL_SCAN_NEXT_TRACK   = 0x04,
    CONSUMER_CTRL_SCAN_PREV_TRACK   = 0x08,
    CONSUMER_CTRL_VOL_DW            = 0x10,
    CONSUMER_CTRL_VOL_UP            = 0x20,
    CONSUMER_CTRL_AC_FORWARD        = 0x40,
    CONSUMER_CTRL_AC_BACK           = 0x80,    
} consumer_control_t;

static void on_hids_evt(ble_hids_t * p_hids, ble_hids_evt_t * p_evt);

/**@brief Callback function for asserts in the SoftDevice.
 *
 * @details This function will be called in case of an assert in the SoftDevice.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyze
 *          how your product is supposed to react in case of Assert.
 * @warning On assert from the SoftDevice, the system can only recover on reset.
 *
 * @param[in]   line_num   Line number of the failing ASSERT call.
 * @param[in]   file_name  File name of the failing ASSERT call.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name)
{
    app_error_handler(DEAD_BEEF, line_num, p_file_name);
}


/**@brief Function for handling Service errors.
 *
 * @details A pointer to this function will be passed to each service which may need to inform the
 *          application about an error.
 *
 * @param[in]   nrf_error   Error code containing information about what went wrong.
 */
static void service_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}


/**@brief Function for handling advertising errors.
 *
 * @param[in] nrf_error  Error code containing information about what went wrong.
 */
static void ble_advertising_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}


/**@brief Function for performing a battery measurement, and update the Battery Level characteristic in the Battery Service.
 */
static void battery_level_update(void)
{
    uint32_t err_code;
    uint8_t  battery_level;
    
    battery_level = (uint8_t)0x55;

    err_code = ble_bas_battery_level_update(&m_bas, battery_level);
    if ((err_code != NRF_SUCCESS) &&
        (err_code != NRF_ERROR_INVALID_STATE) &&
        (err_code != BLE_ERROR_NO_TX_BUFFERS) &&
        (err_code != BLE_ERROR_GATTS_SYS_ATTR_MISSING)
    )
    {
        APP_ERROR_HANDLER(err_code);
    }
}


/**@brief Function for handling the Battery measurement timer timeout.
 *
 * @details This function will be called each time the battery level measurement timer expires.
 *
 * @param[in]   p_context   Pointer used for passing some arbitrary information (context) from the
 *                          app_start_timer() call to the timeout handler.
 */
static void battery_level_meas_timeout_handler(void * p_context)
{
    UNUSED_PARAMETER(p_context);
    battery_level_update();
}

/**@brief Function for the Timer initialization.
 *
 * @details Initializes the timer module.
 */
static void timers_init(void)
{
    uint32_t err_code;

    // Initialize timer module, making it use the scheduler.
    APP_TIMER_INIT(APP_TIMER_PRESCALER, APP_TIMER_MAX_TIMERS, APP_TIMER_OP_QUEUE_SIZE, NULL);

    // Create battery timer.
    err_code = app_timer_create(&m_battery_timer_id,
                                APP_TIMER_MODE_REPEATED,
                                battery_level_meas_timeout_handler);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for the GAP initialization.
 *
 * @details This function sets up all the necessary GAP (Generic Access Profile) parameters of the
 *          device including the device name, appearance, and the preferred connection parameters.
 */
static void gap_params_init(void)
{
    uint32_t                err_code;
    ble_gap_conn_params_t   gap_conn_params;
    ble_gap_conn_sec_mode_t sec_mode;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

    err_code = sd_ble_gap_device_name_set(&sec_mode,
                                          (const uint8_t *)DEVICE_NAME,
                                          strlen(DEVICE_NAME));
    APP_ERROR_CHECK(err_code);

    err_code = sd_ble_gap_appearance_set(BLE_APPEARANCE_HID_KEYBOARD);
    APP_ERROR_CHECK(err_code);

    memset(&gap_conn_params, 0, sizeof(gap_conn_params));

    gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
    gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
    gap_conn_params.slave_latency     = SLAVE_LATENCY;
    gap_conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;

    err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for initializing Device Information Service.
 */
static void dis_init(void)
{
    uint32_t         err_code;
    ble_dis_init_t   dis_init_obj;
    ble_dis_pnp_id_t pnp_id;

    pnp_id.vendor_id_source = PNP_ID_VENDOR_ID_SOURCE;
    pnp_id.vendor_id        = PNP_ID_VENDOR_ID;
    pnp_id.product_id       = PNP_ID_PRODUCT_ID;
    pnp_id.product_version  = PNP_ID_PRODUCT_VERSION;

    memset(&dis_init_obj, 0, sizeof(dis_init_obj));

    ble_srv_ascii_to_utf8(&dis_init_obj.manufact_name_str, MANUFACTURER_NAME);
    dis_init_obj.p_pnp_id = &pnp_id;

    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&dis_init_obj.dis_attr_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&dis_init_obj.dis_attr_md.write_perm);

    err_code = ble_dis_init(&dis_init_obj);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for initializing Battery Service.
 */
static void bas_init(void)
{
    uint32_t       err_code;
    ble_bas_init_t bas_init_obj;

    memset(&bas_init_obj, 0, sizeof(bas_init_obj));

    bas_init_obj.evt_handler          = NULL;
    bas_init_obj.support_notification = true;
    bas_init_obj.p_report_ref         = NULL;
    bas_init_obj.initial_batt_level   = 100;

    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&bas_init_obj.battery_level_char_attr_md.cccd_write_perm);
    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&bas_init_obj.battery_level_char_attr_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&bas_init_obj.battery_level_char_attr_md.write_perm);

    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&bas_init_obj.battery_level_report_read_perm);

    err_code = ble_bas_init(&m_bas, &bas_init_obj);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for initializing HID Service.
 */
static void hids_init(void)
{
    uint32_t                   err_code;
    ble_hids_init_t            hids_init_obj;
    ble_hids_inp_rep_init_t    input_report_array[2];
    ble_hids_inp_rep_init_t  * p_input_report;
    ble_hids_outp_rep_init_t   output_report_array[1];
    ble_hids_outp_rep_init_t * p_output_report;
    uint8_t                    hid_info_flags;

    static uint8_t report_map_data[] =
    {
        0x05, 0x01,                 // Usage Page (Generic Desktop)
        0x09, 0x06,                 // Usage (Keyboard)
        0xA1, 0x01,                 // Collection (Application)
		0x85, INPUT_REP_REF_ID,     //     Report Id (1)
        0x05, 0x07,                 //     Usage Page (Key Codes)
        0x19, 0xe0,                 //     Usage Minimum (224)
        0x29, 0xe7,                 //     Usage Maximum (231)
        0x15, 0x00,                 //     Logical Minimum (0)
        0x25, 0x01,                 //     Logical Maximum (1)
        0x75, 0x01,                 //     Report Size (1)
        0x95, 0x08,                 //     Report Count (8)
        0x81, 0x02,                 //     Input (Data, Variable, Absolute)

        0x95, 0x01,                 //     Report Count (1)
        0x75, 0x08,                 //     Report Size (8)
        0x81, 0x01,                 //     Input (Constant) reserved byte(1)

        0x95, 0x05,                 //     Report Count (5)
        0x75, 0x01,                 //     Report Size (1)
        0x05, 0x08,                 //     Usage Page (Page# for LEDs)
        0x19, 0x01,                 //     Usage Minimum (1)
        0x29, 0x05,                 //     Usage Maximum (5)
        0x91, 0x02,                 //     Output (Data, Variable, Absolute), Led report
        0x95, 0x01,                 //     Report Count (1)
        0x75, 0x03,                 //     Report Size (3)
        0x91, 0x01,                 //     Output (Data, Variable, Absolute), Led report padding

        0x95, 0x06,                 //     Report Count (6)
        0x75, 0x08,                 //     Report Size (8)
        0x15, 0x00,                 //     Logical Minimum (0)
        0x25, 0x65,                 //     Logical Maximum (101)
        0x05, 0x07,                 //     Usage Page (Key codes)
        0x19, 0x00,                 //     Usage Minimum (0)
        0x29, 0x65,                 //     Usage Maximum (101)
        0x81, 0x00,                 //     Input (Data, Array) Key array(6 bytes)

        0x09, 0x05,                 //     Usage (Vendor Defined)
        0x15, 0x00,                 //     Logical Minimum (0)
        0x26, 0xFF, 0x00,           //     Logical Maximum (255)
        0x75, 0x08,                 //     Report Count (2)
        0x95, 0x02,                 //     Report Size (8 bit)
        0xB1, 0x02,                 //     Feature (Data, Variable, Absolute)
        0xC0,                       // End Collection (Application)
		
        // Report ID 3: Advanced buttons
        0x05, 0x0C,                     // Usage Page (Consumer)
        0x09, 0x01,                     // Usage (Consumer Control)
        0xA1, 0x01,                     // Collection (Application)
        0x85, INPUT_CC_REP_REF_ID,      //     Report Id (2)
        0x15, 0x00,                     //     Logical minimum (0)
        0x25, 0x01,                     //     Logical maximum (1)
        0x75, 0x01,                     //     Report Size (1)
        0x95, 0x01,                     //     Report Count (1)

        0x09, 0xCD,                     //     Usage (Play/Pause)
        0x81, 0x02,                     //     Input (Data,Value,Relative,Bit Field)
        0x0A, 0x83, 0x01,               //     Usage (AL Consumer Control Configuration)
        0x81, 0x02,                     //     Input (Data,Value,Relative,Bit Field)
        0x09, 0xB5,                     //     Usage (Scan Next Track)
        0x81, 0x02,                     //     Input (Data,Value,Relative,Bit Field)
        0x09, 0xB6,                     //     Usage (Scan Previous Track)
        0x81, 0x02,                     //     Input (Data,Value,Relative,Bit Field)

        0x09, 0xEA,                     //     Usage (Volume Down)
        0x81, 0x02,                     //     Input (Data,Value,Relative,Bit Field)
        0x09, 0xE9,                     //     Usage (Volume Up)
        0x81, 0x02,                     //     Input (Data,Value,Relative,Bit Field)
        0x0A, 0x25, 0x02,               //     Usage (AC Forward)
        0x81, 0x02,                     //     Input (Data,Value,Relative,Bit Field)
        0x0A, 0x24, 0x02,               //     Usage (AC Back)
        0x81, 0x02,                     //     Input (Data,Value,Relative,Bit Field)
        0xC0                            // End Collection		
    };

    // Initialize HID Service - keyboard descriptor input and output report
    p_input_report                      = &input_report_array[INPUT_REPORT_KEYS_INDEX];
    p_input_report->max_len             = INPUT_REPORT_KEYS_MAX_LEN;
    p_input_report->rep_ref.report_id   = INPUT_REP_REF_ID;
    p_input_report->rep_ref.report_type = BLE_HIDS_REP_TYPE_INPUT;

    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&p_input_report->security_mode.cccd_write_perm);
    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&p_input_report->security_mode.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&p_input_report->security_mode.write_perm);	
    p_output_report                      = &output_report_array[OUTPUT_REPORT_INDEX];
    p_output_report->max_len             = OUTPUT_REPORT_MAX_LEN;
    p_output_report->rep_ref.report_id   = OUTPUT_REP_REF_ID;
    p_output_report->rep_ref.report_type = BLE_HIDS_REP_TYPE_OUTPUT;

    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&p_output_report->security_mode.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&p_output_report->security_mode.write_perm);
	
    // 	Initialize HID Service - ConsumerControl
	
    // Initialize HID Service
    p_input_report                      = &input_report_array[INPUT_CCONTROL_KEYS_INDEX];
    p_input_report->max_len             = INPUT_CC_REPORT_KEYS_MAX_LEN;
    p_input_report->rep_ref.report_id   = INPUT_CC_REP_REF_ID;
    p_input_report->rep_ref.report_type = BLE_HIDS_REP_TYPE_INPUT;

    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&p_input_report->security_mode.cccd_write_perm);
    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&p_input_report->security_mode.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&p_input_report->security_mode.write_perm);	

    hid_info_flags = HID_INFO_FLAG_REMOTE_WAKE_MSK | HID_INFO_FLAG_NORMALLY_CONNECTABLE_MSK;

    memset(&hids_init_obj, 0, sizeof(hids_init_obj));

    hids_init_obj.evt_handler                    = on_hids_evt;
    hids_init_obj.error_handler                  = service_error_handler;
    hids_init_obj.is_kb                          = true;
    hids_init_obj.is_mouse                       = false;
    hids_init_obj.inp_rep_count                  = 2;
    hids_init_obj.p_inp_rep_array                = input_report_array;
    hids_init_obj.outp_rep_count                 = 1;
    hids_init_obj.p_outp_rep_array               = output_report_array;
    hids_init_obj.feature_rep_count              = 0;
    hids_init_obj.p_feature_rep_array            = NULL;
    hids_init_obj.rep_map.data_len               = sizeof(report_map_data);
    hids_init_obj.rep_map.p_data                 = report_map_data;
    hids_init_obj.hid_information.bcd_hid        = BASE_USB_HID_SPEC_VERSION;
    hids_init_obj.hid_information.b_country_code = 0;
    hids_init_obj.hid_information.flags          = hid_info_flags;
    hids_init_obj.included_services_count        = 0;
    hids_init_obj.p_included_services_array      = NULL;

    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&hids_init_obj.rep_map.security_mode.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&hids_init_obj.rep_map.security_mode.write_perm);
    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&hids_init_obj.hid_information.security_mode.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&hids_init_obj.hid_information.security_mode.write_perm);

    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(
        &hids_init_obj.security_mode_boot_kb_inp_rep.cccd_write_perm);
    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&hids_init_obj.security_mode_boot_kb_inp_rep.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&hids_init_obj.security_mode_boot_kb_inp_rep.write_perm);
    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&hids_init_obj.security_mode_boot_kb_outp_rep.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&hids_init_obj.security_mode_boot_kb_outp_rep.write_perm);

    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&hids_init_obj.security_mode_protocol.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&hids_init_obj.security_mode_protocol.write_perm);
    BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&hids_init_obj.security_mode_ctrl_point.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(&hids_init_obj.security_mode_ctrl_point.write_perm);

    err_code = ble_hids_init(&m_hids, &hids_init_obj);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for initializing services that will be used by the application.
 */
static void services_init(void)
{
    dis_init();
    bas_init();
    hids_init();
}


/**@brief Function for handling a Connection Parameters error.
 *
 * @param[in]   nrf_error   Error code containing information about what went wrong.
 */
static void conn_params_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}


/**@brief Function for initializing the Connection Parameters module.
 */
static void conn_params_init(void)
{
    uint32_t               err_code;
    ble_conn_params_init_t cp_init;

    memset(&cp_init, 0, sizeof(cp_init));

    cp_init.p_conn_params                  = NULL;
    cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
    cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
    cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
    cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
    cp_init.disconnect_on_fail             = false;
    cp_init.evt_handler                    = NULL;
    cp_init.error_handler                  = conn_params_error_handler;

    err_code = ble_conn_params_init(&cp_init);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for starting timers.
 */
static void timers_start(void)
{
    uint32_t err_code;

    err_code = app_timer_start(m_battery_timer_id, BATTERY_LEVEL_MEAS_INTERVAL, NULL);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling the HID Report Characteristic Write event.
 *
 * @param[in]   p_evt   HID service event.
 */
static void on_hid_rep_char_write(ble_hids_evt_t *p_evt)
{
    if (p_evt->params.char_write.char_id.rep_type == BLE_HIDS_REP_TYPE_OUTPUT)
    {
        uint8_t  report_val;
        uint8_t  report_index = p_evt->params.char_write.char_id.rep_index;

        if (report_index == OUTPUT_REPORT_INDEX)
        {
            // This code assumes that the outptu report is one byte long. Hence the following
            // static assert is made.
            STATIC_ASSERT(OUTPUT_REPORT_MAX_LEN == 1);

            APP_ERROR_CHECK(ble_hids_outp_rep_get(&m_hids,
                                             report_index,
                                             OUTPUT_REPORT_MAX_LEN,
                                             0,
                                             &report_val));            
        }
    }
}


/**@brief Function for putting the chip into sleep mode.
 *
 * @note This function will not return.
 */
static void sleep_mode_enter(void)
{
    /* Do not go to system off, just keep idling in WFE with wakeup on button */
}


/**@brief Function for handling HID events.
 *
 * @details This function will be called for all HID events which are passed to the application.
 *
 * @param[in]   p_hids  HID service structure.
 * @param[in]   p_evt   Event received from the HID service.
 */
static void on_hids_evt(ble_hids_t * p_hids, ble_hids_evt_t *p_evt)
{
    switch (p_evt->evt_type)
    {
        case BLE_HIDS_EVT_BOOT_MODE_ENTERED:
            m_in_boot_mode = true;
            break;

        case BLE_HIDS_EVT_REPORT_MODE_ENTERED:
            m_in_boot_mode = false;
            break;

        case BLE_HIDS_EVT_REP_CHAR_WRITE:
            on_hid_rep_char_write(p_evt);
            break;

        case BLE_HIDS_EVT_NOTIF_ENABLED:
        {
            dm_service_context_t   service_context;
            service_context.service_type = DM_PROTOCOL_CNTXT_GATT_SRVR_ID;
            service_context.context_data.len = 0;
            service_context.context_data.p_data = NULL;

            if (m_in_boot_mode)
            {
                // Protocol mode is Boot Protocol mode.
                if (
                    p_evt->params.notification.char_id.uuid
                    ==
                    BLE_UUID_BOOT_KEYBOARD_INPUT_REPORT_CHAR
                )
                {
                    // The notification of boot keyboard input report has been enabled.
                    // Save the system attribute (CCCD) information into the flash.
                    uint32_t err_code;

                    err_code = dm_service_context_set(&m_bonded_peer_handle, &service_context);
                    if (err_code != NRF_ERROR_INVALID_STATE)
                    {
                        APP_ERROR_CHECK(err_code);
                    }
                    else
                    {
                        // The system attributes could not be written to the flash because
                        // the connected central is not a new central. The system attributes
                        // will only be written to flash only when disconnected from this central.
                        // Do nothing now.
                    }
                }
                else
                {
                    // Do nothing.
                }
            }
            else if (p_evt->params.notification.char_id.rep_type == BLE_HIDS_REP_TYPE_INPUT && p_evt->params.notification.char_id.rep_index == INPUT_CC_REP_REF_ID)
            {
                // The protocol mode is Report Protocol mode. And the CCCD for the input report
                // is changed. It is now time to store all the CCCD information (system
                // attributes) into the flash.
                uint32_t err_code;

                err_code = dm_service_context_set(&m_bonded_peer_handle, &service_context);
                if (err_code != NRF_ERROR_INVALID_STATE)
                {
                    APP_ERROR_CHECK(err_code);
                }
                else
                {
                    // The system attributes could not be written to the flash because
                    // the connected central is not a new central. The system attributes
                    // will only be written to flash only when disconnected from this central.
                    // Do nothing now.
                }
            }
            else
            {
                // The notification of the report that was enabled by the central is not interesting
                // to this application. So do nothing.
            }
            break;
        }

        default:
            // No implementation needed.
            break;
    }
}


/**@brief Function for handling advertising events.
 *
 * @details This function will be called for advertising events which are passed to the application.
 *
 * @param[in] ble_adv_evt  Advertising event.
 */
static void on_adv_evt(ble_adv_evt_t ble_adv_evt)
{
    uint32_t err_code;

    switch (ble_adv_evt)
    {
        case BLE_ADV_EVT_DIRECTED:
            err_code = bsp_indication_set(BSP_INDICATE_ADVERTISING_DIRECTED);
            APP_ERROR_CHECK(err_code);
            break;
        case BLE_ADV_EVT_FAST:
            err_code = bsp_indication_set(BSP_INDICATE_ADVERTISING);
            APP_ERROR_CHECK(err_code);
            break;
        case BLE_ADV_EVT_SLOW:
            err_code = bsp_indication_set(BSP_INDICATE_ADVERTISING_SLOW);
            APP_ERROR_CHECK(err_code);
            break;
        case BLE_ADV_EVT_FAST_WHITELIST:
            err_code = bsp_indication_set(BSP_INDICATE_ADVERTISING_WHITELIST);
            APP_ERROR_CHECK(err_code);
            break;
        case BLE_ADV_EVT_SLOW_WHITELIST:
            err_code = bsp_indication_set(BSP_INDICATE_ADVERTISING_WHITELIST);
            APP_ERROR_CHECK(err_code);
            break;
        case BLE_ADV_EVT_IDLE:
            sleep_mode_enter();
            break;

        case BLE_ADV_EVT_WHITELIST_REQUEST:
        {
            ble_gap_whitelist_t whitelist;
            ble_gap_addr_t    * p_whitelist_addr[BLE_GAP_WHITELIST_ADDR_MAX_COUNT];
            ble_gap_irk_t     * p_whitelist_irk[BLE_GAP_WHITELIST_IRK_MAX_COUNT];

            whitelist.addr_count = BLE_GAP_WHITELIST_ADDR_MAX_COUNT;
            whitelist.irk_count  = BLE_GAP_WHITELIST_IRK_MAX_COUNT;
            whitelist.pp_addrs   = p_whitelist_addr;
            whitelist.pp_irks    = p_whitelist_irk;

            err_code = dm_whitelist_create(&m_app_handle, &whitelist);
            APP_ERROR_CHECK(err_code);

            err_code = ble_advertising_whitelist_reply(&whitelist);
            APP_ERROR_CHECK(err_code);
            break;
        }
        case BLE_ADV_EVT_PEER_ADDR_REQUEST:
        {
            ble_gap_addr_t peer_address;

            // Only Give peer address if we have a handle to the bonded peer.
            if(m_bonded_peer_handle.appl_id != DM_INVALID_ID)
            {
                            
                err_code = dm_peer_addr_get(&m_bonded_peer_handle, &peer_address);
                APP_ERROR_CHECK(err_code);

                err_code = ble_advertising_peer_addr_reply(&peer_address);
                APP_ERROR_CHECK(err_code);
                
            }
            break;
        }
        default:
            break;
    }
}


/**@brief Function for handling the Application's BLE Stack events.
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 */
static void on_ble_evt(ble_evt_t * p_ble_evt)
{
    uint32_t                              err_code;
    ble_gatts_rw_authorize_reply_params_t auth_reply;

    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GAP_EVT_CONNECTED:
            err_code = bsp_indication_set(BSP_INDICATE_CONNECTED);        
            APP_ERROR_CHECK(err_code);

            m_conn_handle      = p_ble_evt->evt.gap_evt.conn_handle;
            break;

        case BLE_EVT_TX_COMPLETE:
            break;

        case BLE_GAP_EVT_DISCONNECTED:

            m_conn_handle = BLE_CONN_HANDLE_INVALID;

            // disabling alert 3. signal - used for capslock ON
            err_code = bsp_indication_set(BSP_INDICATE_IDLE);
            APP_ERROR_CHECK(err_code);
            break;

        case BLE_EVT_USER_MEM_REQUEST:
            err_code = sd_ble_user_mem_reply(m_conn_handle, NULL);
            APP_ERROR_CHECK(err_code);
            break;

        case BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST:
            if(p_ble_evt->evt.gatts_evt.params.authorize_request.type
               != BLE_GATTS_AUTHORIZE_TYPE_INVALID)
            {
                if ((p_ble_evt->evt.gatts_evt.params.authorize_request.request.write.op
                     == BLE_GATTS_OP_PREP_WRITE_REQ)
                    || (p_ble_evt->evt.gatts_evt.params.authorize_request.request.write.op
                     == BLE_GATTS_OP_EXEC_WRITE_REQ_NOW)
                    || (p_ble_evt->evt.gatts_evt.params.authorize_request.request.write.op
                     == BLE_GATTS_OP_EXEC_WRITE_REQ_CANCEL))
                {
                    if (p_ble_evt->evt.gatts_evt.params.authorize_request.type
                        == BLE_GATTS_AUTHORIZE_TYPE_WRITE)
                    {
                    auth_reply.type = BLE_GATTS_AUTHORIZE_TYPE_WRITE;
                    }
                    else
                    {
                        auth_reply.type = BLE_GATTS_AUTHORIZE_TYPE_READ;
                    }
                    auth_reply.params.write.gatt_status = APP_FEATURE_NOT_SUPPORTED;
                    err_code = sd_ble_gatts_rw_authorize_reply(m_conn_handle,&auth_reply);
                    APP_ERROR_CHECK(err_code);
                }
            }
            break;

        case BLE_GATTC_EVT_TIMEOUT:
        case BLE_GATTS_EVT_TIMEOUT:
            // Disconnect on GATT Server and Client timeout events.
            err_code = sd_ble_gap_disconnect(m_conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            APP_ERROR_CHECK(err_code);
            break;

        default:
            // No implementation needed.
            break;
    }
}


/**@brief   Function for dispatching a BLE stack event to all modules with a BLE stack event handler.
 *
 * @details This function is called from the scheduler in the main loop after a BLE stack
 *          event has been received.
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 */
static void ble_evt_dispatch(ble_evt_t * p_ble_evt)
{
    dm_ble_evt_handler(p_ble_evt);
    on_ble_evt(p_ble_evt);
    ble_advertising_on_ble_evt(p_ble_evt);
    ble_conn_params_on_ble_evt(p_ble_evt);
    ble_hids_on_ble_evt(&m_hids, p_ble_evt);
    ble_bas_on_ble_evt(&m_bas, p_ble_evt);
}


/**@brief   Function for dispatching a system event to interested modules.
 *
 * @details This function is called from the System event interrupt handler after a system
 *          event has been received.
 *
 * @param[in]   sys_evt   System stack event.
 */
static void sys_evt_dispatch(uint32_t sys_evt)
{
    pstorage_sys_event_handler(sys_evt);
    ble_advertising_on_sys_evt(sys_evt);
}


/**@brief Function for initializing the BLE stack.
 *
 * @details Initializes the SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init(void)
{
    uint32_t err_code;

    // Initialize the SoftDevice handler module.
    SOFTDEVICE_HANDLER_INIT(NRF_CLOCK_LFCLKSRC_XTAL_20_PPM, NULL);

    // Enable BLE stack 
    ble_enable_params_t ble_enable_params;
    memset(&ble_enable_params, 0, sizeof(ble_enable_params));
#if (defined(S130) || defined(S132))
    ble_enable_params.gatts_enable_params.attr_tab_size   = BLE_GATTS_ATTR_TAB_SIZE_DEFAULT;
#endif
    ble_enable_params.gatts_enable_params.service_changed = IS_SRVC_CHANGED_CHARACT_PRESENT;
    err_code = sd_ble_enable(&ble_enable_params);
    APP_ERROR_CHECK(err_code);

    // Register with the SoftDevice handler module for BLE events.
    err_code = softdevice_ble_evt_handler_set(ble_evt_dispatch);
    APP_ERROR_CHECK(err_code);

    // Register with the SoftDevice handler module for BLE events.
    err_code = softdevice_sys_evt_handler_set(sys_evt_dispatch);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for sending consumer control data
 */
static uint32_t consumer_control_send(consumer_control_t cmd)
{
    return ble_hids_inp_rep_send(&m_hids, INPUT_CCONTROL_KEYS_INDEX, INPUT_CC_REPORT_KEYS_MAX_LEN, (uint8_t*)&cmd);
}

static uint32_t hid_kbd_send_string(void)
{
    // Scan codes for hid: http://download.microsoft.com/download/1/6/1/161ba512-40e2-4cc9-843a-923143f3456c/translate.pdf
    uint8_t temp_buf[INPUT_REPORT_KEYS_MAX_LEN] = {0x00, 0x00, 0xb, 0xc, 0x2c, 0x0, 0x0, 0x0};   
    return ble_hids_inp_rep_send(&m_hids, INPUT_REPORT_KEYS_INDEX, INPUT_REPORT_KEYS_MAX_LEN, (uint8_t*)temp_buf);
}

static uint32_t hid_kbd_send_release(void)
{
    uint8_t temp_buf[INPUT_REPORT_KEYS_MAX_LEN] = {0, 0, 0, 0, 0, 0, 0, 0};   
    return ble_hids_inp_rep_send(&m_hids, INPUT_REPORT_KEYS_INDEX, INPUT_REPORT_KEYS_MAX_LEN, (uint8_t*)temp_buf);
}


/**@brief Function for initializing the Advertising functionality.
 */
static void advertising_init(void)
{    
    uint8_t        adv_flags;    

    adv_flags                         = BLE_GAP_ADV_FLAGS_LE_ONLY_LIMITED_DISC_MODE;
    m_advdata.name_type               = BLE_ADVDATA_FULL_NAME;
    m_advdata.include_appearance      = true;
    m_advdata.flags                   = adv_flags;
    m_advdata.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
    m_advdata.uuids_complete.p_uuids  = m_adv_uuids;
    ble_adv_modes_config_t options =
    {
        BLE_ADV_WHITELIST_ENABLED,
        BLE_ADV_DIRECTED_ENABLED,
        BLE_ADV_DIRECTED_SLOW_DISABLED, 0,0,
        BLE_ADV_FAST_ENABLED, APP_ADV_FAST_INTERVAL, APP_ADV_FAST_TIMEOUT,
        BLE_ADV_SLOW_ENABLED, APP_ADV_SLOW_INTERVAL, APP_ADV_SLOW_TIMEOUT
    };

    uint32_t err_code = ble_advertising_init(&m_advdata, NULL, &options, on_adv_evt, ble_advertising_error_handler);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for handling the Device Manager events.
 *
 * @param[in]   p_evt   Data associated to the device manager event.
 */
static uint32_t device_manager_evt_handler(dm_handle_t const    * p_handle,
                                           dm_event_t const     * p_event,
                                           ret_code_t           event_result)
{
    APP_ERROR_CHECK(event_result);

    switch(p_event->event_id)
    {
        case DM_EVT_DEVICE_CONTEXT_LOADED: // Fall through.
        case DM_EVT_SECURITY_SETUP_COMPLETE:
            m_bonded_peer_handle = (*p_handle);
            break;
    }

    return NRF_SUCCESS;
}


/**@brief Function for the Device Manager initialization.
 *
 * @param[in] erase_bonds  Indicates whether bonding information should be cleared from
 *                         persistent storage during initialization of the Device Manager.
 */
static void device_manager_init(bool erase_bonds)
{
    uint32_t               err_code;
    dm_init_param_t        init_param = {.clear_persistent_data = erase_bonds};
    dm_application_param_t  register_param;

    // Initialize peer device handle.
    err_code = dm_handle_initialize(&m_bonded_peer_handle);
    APP_ERROR_CHECK(err_code);
    
    // Initialize persistent storage module.
    err_code = pstorage_init();
    APP_ERROR_CHECK(err_code);

    err_code = dm_init(&init_param);
    APP_ERROR_CHECK(err_code);
    
    memset(&register_param.sec_param, 0, sizeof(ble_gap_sec_params_t));

    register_param.sec_param.bond         = SEC_PARAM_BOND;
    register_param.sec_param.mitm         = SEC_PARAM_MITM;
    register_param.sec_param.io_caps      = SEC_PARAM_IO_CAPABILITIES;
    register_param.sec_param.oob          = SEC_PARAM_OOB;
    register_param.sec_param.min_key_size = SEC_PARAM_MIN_KEY_SIZE;
    register_param.sec_param.max_key_size = SEC_PARAM_MAX_KEY_SIZE;
    register_param.evt_handler            = device_manager_evt_handler;
    register_param.service_type           = DM_PROTOCOL_CNTXT_GATT_SRVR_ID;

    err_code = dm_register(&m_app_handle, &register_param);
    APP_ERROR_CHECK(err_code);
}

void app_button_handler(uint8_t pin_no, uint8_t button_action)
{
    // If we're not in a connection, do not do anything. Assume it's wakeup.
    if (m_conn_handle != BLE_CONN_HANDLE_INVALID)
    {
        if (button_action == APP_BUTTON_PUSH)
        {
            switch (pin_no)
            {
                case BSP_BUTTON_0:
                    APP_ERROR_CHECK(consumer_control_send(CONSUMER_CTRL_VOL_DW));
                    break;
                case BSP_BUTTON_1:
                    APP_ERROR_CHECK(consumer_control_send(CONSUMER_CTRL_VOL_UP));
                    break;
                case BSP_BUTTON_2:
                    APP_ERROR_CHECK(hid_kbd_send_string());
                    break;
                case BSP_BUTTON_3:
                    /* Do nothing */ 
                    break;
                default:
                    APP_ERROR_HANDLER(pin_no);
                    break;            
            }
        }
        else if (button_action == APP_BUTTON_RELEASE)
        {
            switch (pin_no)
            {
                case BSP_BUTTON_0:
                /* Fall through*/
                case BSP_BUTTON_1:
                    APP_ERROR_CHECK(consumer_control_send(RELEASE_KEY));
                    break;
                case BSP_BUTTON_2:
                    APP_ERROR_CHECK(hid_kbd_send_release());
                    break;
                case BSP_BUTTON_3:
                    /* Do nothing */
                    break;
                default:
                    APP_ERROR_HANDLER(pin_no);
                    break;              
            }        
        }
    }
}


/**@brief Function for initializing buttons and leds.
 *
 * @param[out] p_erase_bonds  Will be true if the clear bonding button was pressed to wake the application up.
 */
static void buttons_leds_init(void)
{
    const uint32_t detection_delay = 50; // ms
    static app_button_cfg_t button_cfg[4] = 
    {
         {BSP_BUTTON_0, false, BUTTON_PULL, app_button_handler},
         {BSP_BUTTON_1, false, BUTTON_PULL, app_button_handler},
         {BSP_BUTTON_2, false, BUTTON_PULL, app_button_handler},
         {BSP_BUTTON_3, false, BUTTON_PULL, app_button_handler}
    };
    APP_ERROR_CHECK(app_button_init(button_cfg, (sizeof(button_cfg) / sizeof(button_cfg[0])), detection_delay));    
    APP_ERROR_CHECK(app_button_enable());
    
    nrf_gpio_cfg_output(BSP_LED_0);
    nrf_gpio_cfg_output(BSP_LED_1);
    nrf_gpio_cfg_output(BSP_LED_2);
    nrf_gpio_cfg_output(BSP_LED_3);
    NRF_GPIO->OUTSET = (1 << BSP_LED_0) | 1 << BSP_LED_1 | 1 << BSP_LED_2 | 1 << BSP_LED_3;
    
}


/**@brief Function for the Power manager.
 */
static void power_manage(void)
{
#ifdef  HAL_NFC_ENGINEERING_A_FTPAN_WORKAROUND    
    if (!m_nfc_active)
#endif        
    {
        uint32_t err_code = sd_app_evt_wait();
        APP_ERROR_CHECK(err_code);
    }
}

#ifdef NFC_ENABLE
/**@brief Function for re-initializing the Advertising module with fast advertising mode enabled.
 */
static void advertising_enable(void)
{
    uint32_t      err_code;

    ble_adv_modes_config_t options    = {0};
    options.ble_adv_fast_enabled  = BLE_ADV_FAST_ENABLED;
    options.ble_adv_fast_interval = APP_ADV_FAST_INTERVAL;
    options.ble_adv_fast_timeout  = APP_ADV_FAST_TIMEOUT;

    err_code = ble_advertising_init(&m_advdata, NULL, &options, on_adv_evt, NULL);
    APP_ERROR_CHECK(err_code);
}

static void nfc_callback(void * context, NfcEvent event, const char *data, size_t dataLength)
{
    (void) context;
    uint32_t err_code;

    switch (event)
    {
        case NFC_EVENT_FIELD_ON:
            LEDS_ON(BSP_LED_3_MASK);
            /* Start advertising to become connectable */
            if (!m_advertising_flag)
            {
                m_advertising_flag  = 1;
                advertising_enable();
                err_code = ble_advertising_start(BLE_ADV_MODE_FAST);
                APP_ERROR_CHECK(err_code);
            }
            break;
        case NFC_EVENT_FIELD_OFF:
            LEDS_OFF(BSP_LED_3_MASK);
            break;
        default:
            break;
    }
    return;
}


static void nfc_init(void)
{
    ble_advdata_t adv_data;
    uint8_t *     p_message;
    uint16_t      message_len;
    uint32_t      err_code = NRF_SUCCESS;
    NfcRetval     ret_val;
    

    /* Prepare advdata structure as input to create payload of the NFC message */
    memset((uint8_t *) &adv_data, 0, sizeof(ble_advdata_t));

    adv_data.include_appearance = true;
    adv_data.name_type          = BLE_ADVDATA_FULL_NAME;
#if (NFC_BLE_PAIRING_TYPE == NFC_BLE_OOB_PAIRING)
    adv_data.p_tk_value = &m_oob_auth_key;
#endif /* NFC_BLE_PAIRING_TYPE */

#if ((NFC_BLE_PAIR_MSG_CONFIG_MSG_TYPE == NFC_BLE_PAIR_MSG_BLUETOOTH_LE_SHORT) || \
     (NFC_BLE_PAIR_MSG_CONFIG_MSG_TYPE == NFC_BLE_PAIR_MSG_BLUETOOTH_LE_FULL))

    adv_data.include_ble_device_addr = true;
    adv_data.le_role                 = BLE_ADVDATA_ROLE_ONLY_PERIPH;
    adv_data.flags                   = BLE_GAP_ADV_FLAG_BR_EDR_NOT_SUPPORTED;

#elif ((NFC_BLE_PAIR_MSG_CONFIG_MSG_TYPE == NFC_BLE_PAIR_MSG_BLUETOOTH_EP_SHORT) || \
       (NFC_BLE_PAIR_MSG_CONFIG_MSG_TYPE == NFC_BLE_PAIR_MSG_BLUETOOTH_EP_FULL))

    uint8_t sec_mgr_oob_flags = (AD_TYPE_SEC_MGR_OOB_FLAG_SET   << AD_TYPE_SEC_MGR_OOB_FLAG_OOB_DATA_PRESENT_POS) |
                                (AD_TYPE_SEC_MGR_OOB_FLAG_SET   << AD_TYPE_SEC_MGR_OOB_FLAG_OOB_LE_SUPPORTED_POS) |
                                (AD_TYPE_SEC_MGR_OOB_FLAG_CLEAR << AD_TYPE_SEC_MGR_OOB_FLAG_SIM_LE_AND_EP_POS)    |
                                (AD_TYPE_SEC_MGR_OOB_ADDRESS_TYPE_RANDOM << AD_TYPE_SEC_MGR_OOB_FLAG_ADDRESS_TYPE_POS);
    adv_data.p_sec_mgr_oob_flags = &sec_mgr_oob_flags;

#else
    #error "Error. Type of NFC NDEF message used for BLE pairing over NFC not specified."
#endif /* NFC_BLE_PAIR_MSG_CONFIG_MSG_TYPE */

    /* Create the NFC message */
    err_code = nfc_ble_pair_msg_create(&adv_data, &p_message, &message_len);
    APP_ERROR_CHECK(err_code);

    /* Start NFC */
    ret_val = nfcSetup(nfc_callback, NULL);
    if (ret_val != NFC_RETVAL_OK)
    {
        APP_ERROR_CHECK((uint32_t) ret_val);
    }

    ret_val = nfcSetPayload((char *)p_message, message_len);
    if (ret_val != NFC_RETVAL_OK)
    {
        APP_ERROR_CHECK((uint32_t) ret_val);
    }

    ret_val = nfcStartEmulation();
    if (ret_val != NFC_RETVAL_OK)
    {
        APP_ERROR_CHECK((uint32_t) ret_val);
    }

    return;
}
#endif

/**@brief Function for application main entry.
 */
int main(void)
{   
    // Initialize.
    app_trace_init();
    timers_init();   
    ble_stack_init();
    buttons_leds_init();
    #ifdef DEBUG
    device_manager_init(true);
    #else
    device_manager_init(false);
    #endif
    gap_params_init();
    advertising_init();
    services_init();
    conn_params_init();
    
#ifdef NFC_ENABLE
    nfc_init();
#endif    
    // Start execution.
    timers_start();
#ifndef NFC_ENABLE    
    uint32_t err_code = ble_advertising_start(BLE_ADV_MODE_FAST);
    APP_ERROR_CHECK(err_code);
#endif    
    // Enter main loop.
    for (;;)
    {
        power_manage();
    }
}

/**
 * @}
 */
