#include "wled.h"

#ifdef ARDUINO_ARCH_ESP32

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define VIZIVEST_SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define VIZIVEST_COMMAND_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define VIZIVEST_STATUS_UUID  "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

class ViziVestControl : public Usermod {

private:

  BLEServer* bleServer = nullptr;
  BLECharacteristic* commandCharacteristic = nullptr;
  BLECharacteristic* statusCharacteristic = nullptr;

  bool bleStarted = false;

  uint8_t presetGlow   = 1;
  uint8_t presetLeft   = 2;
  uint8_t presetRight  = 3;
  uint8_t presetHazard = 4;
  uint8_t presetOff    = 5;

  void sendStatus(const char* status)
  {
    if (!statusCharacteristic) return;

    statusCharacteristic->setValue(status);
    statusCharacteristic->notify();
  }

  void runPreset(uint8_t preset, const char* status)
  {
    applyPreset(preset, CALL_MODE_DIRECT_CHANGE);
    sendStatus(status);
  }

  void processCommand(String command)
  {
    command.trim();
    command.toUpperCase();

    if (command == "GLOW")
    {
      runPreset(presetGlow, "GLOW");
    }
    else if (command == "LEFT")
    {
      runPreset(presetLeft, "LEFT");
    }
    else if (command == "RIGHT")
    {
      runPreset(presetRight, "RIGHT");
    }
    else if (command == "HAZARD")
    {
      runPreset(presetHazard, "HAZARD");
    }
    else if (command == "OFF")
    {
      runPreset(presetOff, "OFF");
    }
    else
    {
      sendStatus("UNKNOWN");
    }
  }

  class CommandCallbacks : public BLECharacteristicCallbacks
  {
  private:
    ViziVestControl* parent;

  public:
    CommandCallbacks(ViziVestControl* p) : parent(p) {}

    void onWrite(BLECharacteristic* characteristic) override
    {
      String command = characteristic->getValue().c_str();

      if (command.length() == 0)
        return;

      parent->processCommand(command);
    }
  };

  class ServerCallbacks : public BLEServerCallbacks
  {
  private:
    ViziVestControl* parent;

  public:
    ServerCallbacks(ViziVestControl* p) : parent(p) {}

    void onConnect(BLEServer* server) override
    {
      parent->sendStatus("CONNECTED");
    }

    void onDisconnect(BLEServer* server) override
    {
      parent->sendStatus("DISCONNECTED");

      delay(100);
      BLEDevice::startAdvertising();
    }
  };

public:

  void setup() override
  {
    BLEDevice::init("VIZIVEST");

    bleServer = BLEDevice::createServer();

    bleServer->setCallbacks(
      new ServerCallbacks(this)
    );

    BLEService* service =
      bleServer->createService(
        VIZIVEST_SERVICE_UUID
      );

    commandCharacteristic =
      service->createCharacteristic(
        VIZIVEST_COMMAND_UUID,
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR
      );

    statusCharacteristic =
      service->createCharacteristic(
        VIZIVEST_STATUS_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY
      );

    statusCharacteristic->addDescriptor(
      new BLE2902()
    );

    commandCharacteristic->setCallbacks(
      new CommandCallbacks(this)
    );

    statusCharacteristic->setValue("READY");

    service->start();

    BLEAdvertising* advertising =
      BLEDevice::getAdvertising();

    advertising->addServiceUUID(
      VIZIVEST_SERVICE_UUID
    );

    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);
    advertising->setMinPreferred(0x12);

    BLEDevice::startAdvertising();

    bleStarted = true;

    Serial.println();
    Serial.println("================================");
    Serial.println(" VIZIVEST BLE STARTED");
    Serial.println(" Device: VIZIVEST");
    Serial.println(" Commands:");
    Serial.println(" GLOW");
    Serial.println(" LEFT");
    Serial.println(" RIGHT");
    Serial.println(" HAZARD");
    Serial.println(" OFF");
    Serial.println("================================");
  }

  void loop() override
  {
  }

  void addToJsonInfo(JsonObject& root) override
  {
    JsonObject info = root["ViziVest"].to<JsonObject>();

    if (!info)
      info = root.createNestedObject("ViziVest");

    info["BLE"] = bleStarted;
    info["Device"] = "VIZIVEST";
  }
};

static ViziVestControl viziVestControl;

REGISTER_USERMOD(viziVestControl);

#endif
