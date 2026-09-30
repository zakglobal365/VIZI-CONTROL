#include "wled.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

class ViziVestCallbacks : public BLECharacteristicCallbacks
{
private:
  static ViziVestCallbacks* instance;

public:

  static void setInstance(ViziVestCallbacks* obj)
  {
    instance = obj;
  }

  void onWrite(BLECharacteristic* characteristic) override
  {
    std::string value = characteristic->getValue();

    if (value.empty() || !instance)
      return;

    size_t length = value.length();

    if (length >= sizeof(instance->pendingCommand))
      length = sizeof(instance->pendingCommand) - 1;

    memcpy(
      instance->pendingCommand,
      value.c_str(),
      length
    );

    instance->pendingCommand[length] = '\0';
    instance->commandPending = true;
  }

  char pendingCommand[20] = {0};
  volatile bool commandPending = false;
};

ViziVestCallbacks* ViziVestCallbacks::instance = nullptr;


class ViziVestControl : public Usermod
{
private:

  BLEServer* bleServer = nullptr;
  BLEService* bleService = nullptr;
  BLECharacteristic* commandCharacteristic = nullptr;

  ViziVestCallbacks callbacks;

  bool bluetoothStarted = false;

  /*
   * ViziVest preset assignments
   *
   * 1 = GLOW
   * 2 = LEFT
   * 3 = RIGHT
   * 4 = HAZARD
   * 5 = OFF
   */

  uint8_t presetGlow   = 1;
  uint8_t presetLeft   = 2;
  uint8_t presetRight  = 3;
  uint8_t presetHazard = 4;
  uint8_t presetOff    = 5;

  const char* serviceUUID =
    "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";

  const char* commandUUID =
    "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";


  void processCommand()
  {
    if (!callbacks.commandPending)
      return;

    char command[20];

    noInterrupts();

    strncpy(
      command,
      callbacks.pendingCommand,
      sizeof(command)
    );

    command[sizeof(command) - 1] = '\0';

    callbacks.commandPending = false;

    interrupts();

    String cmd = String(command);

    cmd.trim();
    cmd.toUpperCase();

    Serial.print("VIZIVEST BLE COMMAND: ");
    Serial.println(cmd);


    if (cmd == "GLOW")
    {
      applyPreset(
        presetGlow,
        CALL_MODE_DIRECT_CHANGE
      );
    }

    else if (cmd == "LEFT")
    {
      applyPreset(
        presetLeft,
        CALL_MODE_DIRECT_CHANGE
      );
    }

    else if (cmd == "RIGHT")
    {
      applyPreset(
        presetRight,
        CALL_MODE_DIRECT_CHANGE
      );
    }

    else if (cmd == "HAZARD")
    {
      applyPreset(
        presetHazard,
        CALL_MODE_DIRECT_CHANGE
      );
    }

    else if (cmd == "OFF")
    {
      applyPreset(
        presetOff,
        CALL_MODE_DIRECT_CHANGE
      );
    }

    else
    {
      Serial.print("Unknown VIZIVEST command: ");
      Serial.println(cmd);
    }
  }


  void startBluetooth()
  {
    Serial.println("Starting VIZIVEST Bluetooth...");

    BLEDevice::init("VIZIVEST");

    bleServer = BLEDevice::createServer();

    bleService =
      bleServer->createService(serviceUUID);

    commandCharacteristic =
      bleService->createCharacteristic(
        commandUUID,
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR
      );

    callbacks.setInstance(&callbacks);

    commandCharacteristic->setCallbacks(&callbacks);

    commandCharacteristic->setValue("READY");

    bleService->start();

    BLEAdvertising* advertising =
      BLEDevice::getAdvertising();

    advertising->addServiceUUID(serviceUUID);
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);
    advertising->setMinPreferred(0x12);

    BLEDevice::startAdvertising();

    bluetoothStarted = true;

    Serial.println();
    Serial.println("==============================");
    Serial.println(" VIZIVEST BLUETOOTH READY");
    Serial.println(" DEVICE: VIZIVEST");
    Serial.println("==============================");
  }


public:

  void setup() override
  {
    startBluetooth();
  }


  void loop() override
  {
    processCommand();
  }


  void addToJsonInfo(JsonObject& root) override
  {
    JsonObject info =
      root.createNestedObject("ViziVest");

    info["BLE"] = bluetoothStarted;
    info["Device"] = "VIZIVEST";
  }
};


static ViziVestControl viziVestControl;

REGISTER_USERMOD(viziVestControl);
