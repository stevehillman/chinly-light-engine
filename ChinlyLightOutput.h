
#include "esphome.h"
#include "BLEDevice.h"
#include "Chinly_Device.h"
#include <string>

/* Handle the interface between ESPHome and Chinly Light Engines
 *
 */

class ChinlyLightOutput;
class MyAdvertisedDeviceCallbacks;
class ChinlyComponent;

// Globals

#define MAXLIGHTS 16 // Maximum number of lights we can track
#define SCANTIME 10  // How long should we wait for all lights to respond
ChinlyLightDevice* chinlyLightDevice[MAXLIGHTS];
ChinlyLightOutput* chinlyLightOutput[MAXLIGHTS];
int totalLights = 1;
int foundLights = 0;

/* Represent a single instance of a Chinly Light Engine.
 * This maps the Light Engine to a LightOutput component for use by ESPHome
 * Additional methods are added for managing the twinkle and music functions
 * which are otherwise unavailable on a LightOutput device
 *
 */
 
class ChinlyLightOutput : public Component, public LightOutput {
 private:
  ChinlyLightDevice* myChinlyLightDevice;

 public:
  void setup() override {
    // This will be called by App.setup()
  }
  LightTraits get_traits() override {
    // return the traits this light supports
    auto traits = LightTraits();
    traits.set_supports_brightness(true);
    traits.set_supports_rgb(true);
    traits.set_supports_rgb_white_value(false);
    traits.set_supports_color_temperature(false);
    return traits;
  }

  void write_state(LightState *state) override {
    // This will be called by the light to get a new state to be written.
    float red, green, blue;
    // use any of the provided current_values methods
    state->current_values_as_rgb(&red, &green, &blue);
    // Write red, green and blue to HW
    // ...
  }

  void setDevice(ChinlyLightDevice* device) {
    myChinlyLightDevice = device;
  }
};

/**
 * Callback for the BLEScan. Called for each device found
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
/**
 * Called for each advertising BLE server.
 */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {
      foundLights++;
      if (foundLights >= totalLights) {
        // We've found all the lights there are to find 
        BLEDevice::getScan()->stop();
      }
      // Insert our new found light into the array, sorted based on its MAC address
      std::string foundAddress = advertisedDevice.getAddress().toString();
      ESP_LOGD("Chinly","Found device with address %s", foundAddress.c_str());
      int i;
      for (i=0;i < foundLights;i++) {
        if (chinlyLightOutput[i] == nullptr) {
          // At the end of the entries; Add light here
          break;
        }
        if (foundAddress.compare(chinlyLightDevice[i]->getDevice()->getAddress().toString()) < 0) {
          // Insert our new light here
          for(int j=foundLights; j>i; j--) {
            chinlyLightDevice[j] = chinlyLightDevice[j-1];
          }
          break;
        }
      }
      chinlyLightDevice[i] = new ChinlyLightDevice();
      chinlyLightDevice[i]->setDevice(new BLEAdvertisedDevice(advertisedDevice));
    } // Found our server
  } // onResult
}; // MyAdvertisedDeviceCallbacks

/* Manage a collection of one or more ChinlyLightOutputs
 * When setup() is called on this component, it initiates a BLE scan for 
 * all light engines. It returns when all have been found, or SCANTIME has elapsed
 * When engines are found, a ChinlyLightDevice and ChinlyLightOutput object are
 * created. The ChinlyLightObject must be passed back to ESPHome to instantiate a Light object
 * 
 * This allows a single ESP32 to control any number of Light Engines 
 * (I'm probably the only one crazy enough to have more than one though!)
 */

class ChinlyComponent : public Component {
  public:
    ChinlyComponent() {
      ChinlyComponent(1);
    }

    ChinlyComponent(int numLights) {
      totalLights = numLights;
      foundLights = 0;
    }

    ChinlyLightOutput* getLightOutput(int index) {
      return chinlyLightOutput[index];
    }

    ChinlyLightOutput* getLightOutput() {
      return chinlyLightOutput[0];
    }

    void setup() {
      // Retrieve a Scanner and set the callback we want to use to be informed when we
      // have detected a new device.  Specify that we want active scanning and start the
      // scan to run for 10 seconds.
      BLEScan* pBLEScan = BLEDevice::getScan();
      pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
      pBLEScan->setInterval(1349);
      pBLEScan->setWindow(449);
      pBLEScan->setActiveScan(true);
      pBLEScan->start(SCANTIME, false);

      // While the scan is running, check how many devices have been found. Once we
      // reach our number, connect them all and return.
      int i = 0;
      while(i < SCANTIME) {
        delay(1000);
        if (foundLights == totalLights) {
          break;
        }
      }
      for (int j=0; j < foundLights; ) {
        chinlyLightDevice[j]->connect();
        chinlyLightOutput[j] = new ChinlyLightOutput();
        chinlyLightOutput[j]->setDevice(chinlyLightDevice[j]);
      }
    }
};