#include "wled.h"

#ifdef ARDUINO_ARCH_ESP32

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"
#include "esp_bt_defs.h"
#include "esp_gatt_defs.h"

#include <cstring>

class ViziVestControl : public Usermod
{
private:

  // ============================================================
  // VIZIVEST BLE UUIDs
  // ============================================================

  static const uint8_t SERVICE_UUID[16];
  static const uint8_t COMMAND_UUID[16];

  // ============================================================
  // BLE state
  // ============================================================

  static ViziVestControl* instance;

  esp_gatt_if_t gattsInterface = ESP_GATT_IF_NONE;

  uint16_t serviceHandle = 0;
  uint16_t commandHandle = 0;

  bool bluetoothStarted = false;
  bool bluetoothReady = false;

  // Command received from phone.
  volatile bool commandPending = false;
  char pendingCommand[20] = {0};

  // ============================================================
  // WLED preset assignments
  // ============================================================

  uint8_t presetGlow   = 1;
  uint8_t presetLeft   = 2;
  uint8_t presetRight  = 3;
  uint8_t presetHazard = 4;
  uint8_t presetOff    = 5;

  // ============================================================
  // BLE advertising
  // ============================================================

  static esp_ble_adv_params_t advertisingParams;

  static esp_ble_adv_data_t advertisingData;

  // ============================================================
  // BLE GAP callback
  // ============================================================

  static void gapCallback(
    esp_gap_ble_cb_event_t event,
    esp_ble_gap_cb_param_t* param
  )
  {
    if (!instance)
      return;

    switch (event)
    {
      case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:

        esp_ble_gap_start_advertising(
          &advertisingParams
        );

        break;

      case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:

        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS)
        {
          instance->bluetoothReady = true;

          Serial.println(
            "VIZIVEST BLE advertising started"
          );
        }
        else
        {
          Serial.println(
            "VIZIVEST BLE advertising failed"
          );
        }

        break;

      default:
        break;
    }
  }

  // ============================================================
  // BLE GATT callback
  // ============================================================

  static void gattsCallback(
    esp_gatts_cb_event_t event,
    esp_gatt_if_t gatts_if,
    esp_ble_gatts_cb_param_t* param
  )
  {
    if (!instance)
      return;

    switch (event)
    {
      // --------------------------------------------------------
      // BLE application registered
      // --------------------------------------------------------

      case ESP_GATTS_REG_EVT:
      {
        if (param->reg.status != ESP_GATT_OK)
        {
          Serial.println(
            "VIZIVEST BLE registration failed"
          );

          return;
        }

        instance->gattsInterface = gatts_if;

        esp_ble_gap_set_device_name(
          "VIZIVEST"
        );

        esp_ble_gap_config_adv_data(
          &advertisingData
        );

        // Configure service UUID.
        esp_gatt_srvc_id_t serviceId;

        memset(
          &serviceId,
          0,
          sizeof(serviceId)
        );

        serviceId.is_primary = true;

        serviceId.id.inst_id = 0;

        serviceId.id.uuid.len = ESP_UUID_LEN_128;

        memcpy(
          serviceId.id.uuid.uuid.uuid128,
          SERVICE_UUID,
          16
        );

        esp_ble_gatts_create_service(
          gatts_if,
          &serviceId,
          4
        );

        break;
      }

      // --------------------------------------------------------
      // Service created
      // --------------------------------------------------------

      case ESP_GATTS_CREATE_EVT:
      {
        if (param->create.status != ESP_GATT_OK)
          return;

        instance->serviceHandle =
          param->create.service_handle;

        esp_bt_uuid_t commandUuid;

        memset(
          &commandUuid,
          0,
          sizeof(commandUuid)
        );

        commandUuid.len = ESP_UUID_LEN_128;

        memcpy(
          commandUuid.uuid.uuid128,
          COMMAND_UUID,
          16
        );

        uint8_t initialValue[] = "READY";

        esp_attr_value_t commandValue;

        commandValue.attr_max_len =
          sizeof(initialValue);

        commandValue.attr_len =
          sizeof(initialValue) - 1;

        commandValue.attr_value =
          initialValue;

        esp_attr_control_t control;

        control.auto_rsp =
          ESP_GATT_AUTO_RSP;

        esp_ble_gatts_add_char(
          instance->serviceHandle,
          &commandUuid,

          ESP_GATT_PERM_WRITE,

          ESP_GATT_CHAR_PROP_BIT_WRITE |
          ESP_GATT_CHAR_PROP_BIT_WRITE_NR,

          &commandValue,

          &control
        );

        break;
      }

      // --------------------------------------------------------
      // Characteristic created
      // --------------------------------------------------------

      case ESP_GATTS_ADD_CHAR_EVT:
      {
        if (param->add_char.status != ESP_GATT_OK)
          return;

        instance->commandHandle =
          param->add_char.attr_handle;

        esp_ble_gatts_start_service(
          instance->serviceHandle
        );

        Serial.println(
          "VIZIVEST BLE service ready"
        );

        break;
      }

      // --------------------------------------------------------
      // Phone writes a command
      // --------------------------------------------------------

      case ESP_GATTS_WRITE_EVT:
      {
        if (param->write.is_prep)
          break;

        if (
          param->write.handle ==
          instance->commandHandle
        )
        {
          uint16_t length =
            param->write.len;

          if (length >=
              sizeof(instance->pendingCommand))
          {
            length =
              sizeof(instance->pendingCommand) - 1;
          }

          memcpy(
            instance->pendingCommand,
            param->write.value,
            length
          );

          instance->pendingCommand[length] =
            '\0';

          instance->commandPending = true;
        }

        break;
      }

      // --------------------------------------------------------
      // Phone connected
      // --------------------------------------------------------

      case ESP_GATTS_CONNECT_EVT:

        Serial.println(
          "VIZIVEST phone connected"
        );

        break;

      // --------------------------------------------------------
      // Phone disconnected
      // --------------------------------------------------------

      case ESP_GATTS_DISCONNECT_EVT:

        Serial.println(
          "VIZIVEST phone disconnected"
        );

        esp_ble_gap_start_advertising(
          &advertisingParams
        );

        break;

      default:
        break;
    }
  }

  // ============================================================
  // Process commands safely in WLED's main loop
  // ============================================================

  void processPendingCommand()
  {
    if (!commandPending)
      return;

    char command[20];

    noInterrupts();

    strncpy(
      command,
      pendingCommand,
      sizeof(command)
    );

    command[
      sizeof(command) - 1
    ] = '\0';

    commandPending = false;

    interrupts();

    String cmd = String(command);

    cmd.trim();
    cmd.toUpperCase();

    Serial.print(
      "VIZIVEST BLE COMMAND: "
    );

    Serial.println(cmd);

    // ----------------------------------------------------------
    // GLOW
    // ----------------------------------------------------------

    if (cmd == "GLOW")
    {
      applyPreset(
        presetGlow,
        CALL_MODE_DIRECT_CHANGE
      );
    }

    // ----------------------------------------------------------
    // LEFT
    // ----------------------------------------------------------

    else if (cmd == "LEFT")
    {
      applyPreset(
        presetLeft,
        CALL_MODE_DIRECT_CHANGE
      );
    }

    // ----------------------------------------------------------
    // RIGHT
    // ----------------------------------------------------------

    else if (cmd == "RIGHT")
    {
      applyPreset(
        presetRight,
        CALL_MODE_DIRECT_CHANGE
      );
    }

    // ----------------------------------------------------------
    // HAZARD
    // ----------------------------------------------------------

    else if (cmd == "HAZARD")
    {
      applyPreset(
        presetHazard,
        CALL_MODE_DIRECT_CHANGE
      );
    }

    // ----------------------------------------------------------
    // OFF
    // ----------------------------------------------------------

    else if (cmd == "OFF")
    {
      applyPreset(
        presetOff,
        CALL_MODE_DIRECT_CHANGE
      );
    }

    else
    {
      Serial.println(
        "Unknown VIZIVEST command"
      );
    }
  }

  // ============================================================
  // Start Bluetooth
  // ============================================================

  void startBluetooth()
  {
    Serial.println(
      "Starting VIZIVEST Bluetooth..."
    );

    // Release Classic Bluetooth memory.
    // We only need BLE.
    esp_bt_controller_mem_release(
      ESP_BT_MODE_CLASSIC_BT
    );

    esp_bt_controller_config_t btConfig =
      BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    esp_err_t result =
      esp_bt_controller_init(
        &btConfig
      );

    if (
      result != ESP_OK &&
      result != ESP_ERR_INVALID_STATE
    )
    {
      Serial.println(
        "Bluetooth controller init failed"
      );

      return;
    }

    result =
      esp_bt_controller_enable(
        ESP_BT_MODE_BLE
      );

    if (
      result != ESP_OK &&
      result != ESP_ERR_INVALID_STATE
    )
    {
      Serial.println(
        "Bluetooth controller enable failed"
      );

      return;
    }

    result =
      esp_bluedroid_init();

    if (
      result != ESP_OK &&
      result != ESP_ERR_INVALID_STATE
    )
    {
      Serial.println(
        "Bluedroid init failed"
      );

      return;
    }

    result =
      esp_bluedroid_enable();

    if (
      result != ESP_OK &&
      result != ESP_ERR_INVALID_STATE
    )
    {
      Serial.println(
        "Bluedroid enable failed"
      );

      return;
    }

    esp_ble_gap_register_callback(
      gapCallback
    );

    esp_ble_gatts_register_callback(
      gattsCallback
    );

    esp_ble_gatts_app_register(
      0
    );

    bluetoothStarted = true;

    Serial.println(
      "VIZIVEST Bluetooth initialized"
    );
  }

public:

  // ============================================================
  // WLED setup
  // ============================================================

  void setup() override
  {
    instance = this;

    startBluetooth();

    Serial.println();
    Serial.println(
      "=============================="
    );
    Serial.println(
      " VIZIVEST CONTROL"
    );
    Serial.println(
      " BLE NAME: VIZIVEST"
    );
    Serial.println(
      "=============================="
    );
  }

  // ============================================================
  // WLED loop
  // ============================================================

  void loop() override
  {
    processPendingCommand();
  }

  // ============================================================
  // WLED info
  // ============================================================

  void addToJsonInfo(JsonObject& root) override
  {
    JsonObject info =
      root.createNestedObject(
        "ViziVest"
      );

    info["BLE"] =
      bluetoothStarted;

    info["Device"] =
      "VIZIVEST";

    info["Ready"] =
      bluetoothReady;
  }
};


// ============================================================
// UUID definitions
// ============================================================

const uint8_t ViziVestControl::SERVICE_UUID[16] =
{
  0x6E, 0x40, 0x00, 0x01,
  0xB5, 0xA3,
  0xF3, 0x93,
  0xE0, 0xA9,
  0xE5, 0x0E,
  0x24, 0xDC,
  0xCA, 0x9E
};

const uint8_t ViziVestControl::COMMAND_UUID[16] =
{
  0x6E, 0x40, 0x00, 0x02,
  0xB5, 0xA3,
  0xF3, 0x93,
  0xE0, 0xA9,
  0xE5, 0x0E,
  0x24, 0xDC,
  0xCA, 0x9E
};


// ============================================================
// BLE advertising parameters
// ============================================================

esp_ble_adv_params_t ViziVestControl::advertisingParams =
{
  .adv_int_min = 0x20,
  .adv_int_max = 0x40,

  .adv_type =
    ADV_TYPE_IND,

  .own_addr_type =
    BLE_ADDR_TYPE_PUBLIC,

  .peer_addr =
    {0, 0, 0, 0, 0, 0},

  .peer_addr_type =
    BLE_ADDR_TYPE_PUBLIC,

  .channel_map =
    ADV_CHNL_ALL,

  .adv_filter_policy =
    ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY
};


// ============================================================
// BLE advertising data
// ============================================================

esp_ble_adv_data_t ViziVestControl::advertisingData =
{
  .set_scan_rsp = false,

  .include_name = true,

  .include_txpower = true,

  .min_interval = 0x06,

  .max_interval = 0x12,

  .appearance = 0x00,

  .manufacturer_len = 0,

  .p_manufacturer_data = nullptr,

  .service_data_len = 0,

  .p_service_data = nullptr,

  .service_uuid_len = ESP_UUID_LEN_128,

  .p_service_uuid =
    const_cast<uint8_t*>(
      ViziVestControl::SERVICE_UUID
    ),

  .flag =
    ESP_BLE_ADV_FLAG_GEN_DISC |
    ESP_BLE_ADV_FLAG_BREDR_NOT_SPT
};


// ============================================================
// Singleton
// ============================================================

ViziVestControl*
ViziVestControl::instance =
  nullptr;


// ============================================================
// Register WLED usermod
// ============================================================

static ViziVestControl viziVestControl;

REGISTER_USERMOD(viziVestControl);

#endif
