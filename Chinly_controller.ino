#include "BLEDevice.h"
#include "Chinly_Device.h"

  // Globals

  ChinlyLightDevice* lightDevice;
  static BLEAdvertisedDevice* foundDevice;
  static boolean doConnect = false;
  static boolean connected = false;
  static boolean doScan = false;

  /**
   * Scan for BLE servers and find the first one that advertises the service we are looking for.
   */
  class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  /**
     * Called for each advertising BLE server.
     */
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      Serial.print("BLE Advertised Device found: ");
      Serial.println(advertisedDevice.toString().c_str());

      // We have found a device, let us now see if it contains the service we are looking for.
      if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {

        BLEDevice::getScan()->stop();
        foundDevice = new BLEAdvertisedDevice(advertisedDevice);
        doConnect = true;
        doScan = true;

      } // Found our server
    } // onResult
  }; // MyAdvertisedDeviceCallbacks


  // Simple example. Use the ChinlyLightDevice class to manage a single Light Engine
  // This example flashes the light engine off and on every 2 seconds

  // On startup, initiate a scan for devices
  void setup() {
    Serial.begin(115200);
    Serial.println("Starting Arduino BLE Client application...");
    BLEDevice::init("");

    // Retrieve a Scanner and set the callback we want to use to be informed when we
    // have detected a new device.  Specify that we want active scanning and start the
    // scan to run for 5 seconds.
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setInterval(1349);
    pBLEScan->setWindow(449);
    pBLEScan->setActiveScan(true);
    pBLEScan->start(5, false);
  }

  // In the loop, check to see if any devices need connecting to. If so, initiate BLE connection. Leave anything else for
  // the next loop through
  // On every subsequent loop toggle the light engine's power state and sleep for 2 seconds
  void loop() {
    if (doConnect) {
      lightDevice = new ChinlyLightDevice();
      lightDevice->setDevice(foundDevice);
      doConnect = false;
      lightDevice->connect();
      return;
    }
    if (lightDevice->connected()) {
      bool curstate = lightDevice->getState();
      curstate = !curstate;
      lightDevice->setState(curstate);
      delay(2000); 
    }
    
  }
