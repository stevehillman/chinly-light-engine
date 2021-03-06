
#include "esphome.h"
#include "myBLE/myBLEDevice.h"
#include "Chinly_Device.h"
#include <string>

/* Handle the interface between ESPHome and Chinly Light Engines
 *
 */

class ChinlyLightOutput;
class MyAdvertisedDeviceCallbacks;
class ChinlyComponent;

// Constants
const int MAXLIGHTS = 16; // Maximum number of lights we can track
const int SCANTIME = 10;  // How long should we wait for all lights to respond
const int MAXTRIES = 30;  // how many tries should we make to reconnect to lost device. A linear increasing backoff time is used after each attempt
const uint8_t MAX_BRIGHTNESS = 0x64;

// Globals
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
  LightState* light_state;

 public:

  float get_setup_priority() const override { return esphome::setup_priority::LATE; }

  LightState* get_light_state() { return light_state; }

  LightTraits get_traits() override {
    // return the traits this light supports
    auto traits = LightTraits();
    traits.set_supports_brightness(true);
    traits.set_supports_rgb(true);
    traits.set_supports_rgb_white_value(true);
    traits.set_supports_color_temperature(false);
    return traits;
  }

  void write_state(LightState *state) override {
    // This will be called by the light to get a new state to be written.
    float red, green, blue, white, brightness;
    bool power;
    
    // Save our state object for future reference
    light_state = state;

    // use any of the provided current_values methods
    state->current_values_as_rgbw(&red, &green, &blue, &white);
    state->current_values_as_brightness(&brightness);
    state->current_values_as_binary(&power);

    ESP_LOGD("ChinlyOutput","write_state called with R %f, G %f, B %f, W %f, Br %f", red, green, blue, white, brightness);

    if (!myChinlyLightDevice->connected()) {
      ESP_LOGE("ChinlyOutput","Device is disconnected. Unable to update state");
    }

    // If state is "OFF"
    if (!power && (brightness == 0.0f || brightness == 1.0f)) {
      myChinlyLightDevice->setState(false);
    }
    else {
      // Not off. Update color and brightness
      uint32_t color = (static_cast<uint32_t>(red*255)<<24) | (static_cast<uint32_t>(green*255)<<16) | (static_cast<uint32_t>(blue*255)<<8) | static_cast<uint32_t>(white*255) ;
      uint8_t brightness_int = static_cast<uint8_t>(brightness * MAX_BRIGHTNESS);
      myChinlyLightDevice->setBrightnessAndColorAndState(brightness_int,color,true);
    }
  }

  void set_device(ChinlyLightDevice* device) {
    myChinlyLightDevice = device;
  }

  // These last methods expose the underlying effects of the Light Engine. Use them
  // in custom Lambda code in ESPHome to add custom effects. It's the only way to support
  // twinkle, music, etc, without writing custom ESPHome code
  void set_twinkle(int mode) {
    myChinlyLightDevice->setTwinkleSpeed(static_cast<uint8_t>(mode));
  }

  void set_music_mode(int mode) {
    myChinlyLightDevice->setMusicMode(static_cast<uint8_t>(mode));
  }

  void set_effect(int effect, int speed) {
    myChinlyLightDevice->setFunction(static_cast<uint8_t>(effect), static_cast<uint8_t>(speed));
  }
};

/**
 * Callback for the BLEScan. Called for each device found
 * For each device found, sort it by its MAC address into the
 * chinlyLightDevice[] array of devices
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
/**
 * Called for each advertising BLE server.
 */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {
      foundLights++;
      // Insert our new found light into the array, sorted based on its MAC address
      std::string foundAddress = advertisedDevice.getAddress().toString();
      ESP_LOGD("ChinlyScan","Found device with address %s", foundAddress.c_str());
      int i;
      for (i=0;i < foundLights;i++) {
        if (chinlyLightDevice[i] == nullptr) {
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
      ESP_LOGD("ChinlyScan","Inserted at index %d",i);

      if (foundLights >= totalLights) {
        // We've found all the lights there are to find 
        ESP_LOGD("ChinlyScan","Found all lights. Stopping scan");
        BLEDevice::getScan()->stop();
      }
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
  protected:
    int loop_counter;
    int backoff_time;
    int connect_retries[MAXLIGHTS];

  public:
    ChinlyComponent() {
      ChinlyComponent(1);
    }

    ChinlyComponent(int numLights) {
      totalLights = numLights;
      foundLights = 0;
      for (int i =0; i < numLights; i++) {
        chinlyLightOutput[i] = new ChinlyLightOutput();
      }
    }

    ChinlyLightOutput* getLightOutput(int index) {
      return chinlyLightOutput[index];
    }

    ChinlyLightOutput* getLightOutput() {
      return chinlyLightOutput[0];
    }

    void setup() {
      // Set up a BLE Scanner and set the callback we want to use to be informed when we
      // have detected a new device.  Specify that we want active scanning and start the
      // scan to run for 10 seconds.
      BLEDevice::init("");
      loop_counter = 0;
      backoff_time = 1;
      BLEScan* pBLEScan = BLEDevice::getScan();
      pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
      pBLEScan->setInterval(1349);
      pBLEScan->setWindow(449);
      pBLEScan->setActiveScan(true);
      pBLEScan->start(SCANTIME, false);
      
      ESP_LOGD("Chinly","BLE scan complete. Found %d devices", foundLights);
      for (int j=0; j < foundLights; j++ ) {
        connect_retries[j] = 0;
        ESP_LOGD("Chinly","Calling connect() on device at index %d",j);
        if (!chinlyLightDevice[j]->connect()) {
          ESP_LOGE("Chinly","Failed to connect to device at index %d",j);
          connect_retries[j]++;
        }
        chinlyLightOutput[j]->set_device(chinlyLightDevice[j]);
      }
    }

    void loop() {
      loop_counter++;
      if (!(loop_counter % (600 * backoff_time))) {
        // At 10 second intervals (multiplied by number of failures), 
        // check to see if any devices have disconnected and need reconnecting
        loop_counter = 0;
        for (int j=0; j < foundLights; j++) {
          if (!chinlyLightDevice[j]->connected() && connect_retries[j] <= MAXTRIES) {
            ESP_LOGD("Chinly","Calling connect() on device at index %d",j);
            int rc = chinlyLightDevice[j]->connect();
            if (rc) {
              // reconnect was successful
              chinlyLightOutput[j]->write_state(chinlyLightOutput[j]->get_light_state());
              connect_retries[j] = 0;
            } else {
              // Failed. Backoff until it's time to give up
              ESP_LOGE("Chinly","Failed to connect to device at index %d",j);
              connect_retries[j]++;
              if (connect_retries[j] > backoff_time) {
                backoff_time = connect_retries[j];
              }
              if (connect_retries[j] > MAXTRIES) {
                // time to give up
                ESP_LOGE("Chinly","Giving up on device at index %d. Marking as failed.",j);
                backoff_time = 1;
                chinlyLightOutput[j]->mark_failed();
              }
            }
          }
        } // for loop
      }
    }

    void on_shutdown() override {
      ESP_LOGD("Chinly","Shutdown called.");
      for (int j=0; j < foundLights; j++) {
        if (chinlyLightDevice[j] != nullptr && chinlyLightDevice[j]->connected()) {
          ESP_LOGV("ChinlyDevice","Disconnecting.");
          chinlyLightDevice[j]->disconnect();
        }
      }
    }
};