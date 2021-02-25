#ifndef CHINLYDEVICE
#define CHINLYDEVICE
#include "BLEDevice.h"

/*
 * Theory of Operation: We'll use a Light class to represent a single instance of a
 * Chinly Light Engine. 
 * 
 * It'll need the following static methods:
 *   - serviceUUID - return the Service UUID used by all Chinly engines 
 *   - charUUID - return the Characteristic UUID of the handle that updates the engine
 *   - getFunctionList - returns an array (or ENUM?) of available functions
 *   
 * And the following object methods:
 *   - setDevice - set the device object for this Light object
 *   - getId     - return the unique ID of this light (just its device address really)
 *   - connect   - attempt to connect to the device object. Once connected, set the light engine to a known state
 *   - connected - whether we're connected
 *   - getState  - on/off
 *   - setState(bool) - on/off
 *   - getColor - returns color as an 0xRRGGBB hex value
 *   - setColor(uint_32) - set color
 *   - getBrightness
 *   - setBrightness
 *   - getTwinkleSpeed - 0 = off, 1-4 = motor speed
 *   - setTwinkleSpeed
 *   - getMusicMode
 *   - setMusicMode - 0=off, 1-9 = on with mic sensitivity = n
 *   
 *   We'll have a global array of Lights, which will get populated by an initial scan
 */

// The remote service we wish to connect to.
static BLEUUID serviceUUID("0000ffb0-0000-1000-8000-00805f9b34fb");
// The characteristic of the remote service we are interested in.
static BLEUUID charUUID("0000ffb1-0000-1000-8000-00805f9b34fb");


/*
 * Callback for the BLEClient. This gets called whenever the connection state changes
 */

 class ConnectionState : public BLEClientCallbacks {
   public: 
          bool connected = false;
          
   void onConnect(BLEClient* pclient) {
     connected = true;
   }

   void onDisconnect(BLEClient* pclient) {
     connected = false;
   }
 };

/*
 * Main Light Engine class
 */
  class ChinlyLightDevice {

   protected:
     ConnectionState* connectionState;
     BLEAdvertisedDevice* myDevice = nullptr;
     BLEClient*  myClient;
     BLERemoteService* myRemoteService;
     BLERemoteCharacteristic* myRemoteCharacteristic;

     struct chardata_t {
       uint8_t    header; // always 0xa5
       uint8_t     power;
       uint8_t      mode; // 0 = use 'function', 1 = 'on', 4 = 'sound activated'
       uint8_t  function;
       uint8_t    fspeed; // speed that function runs or transitions at
       union {
        struct {
          uint8_t       red;
          uint8_t     green;
          uint8_t      blue;
          uint8_t    filler;
        };
        struct {
          uint32_t    color;
        };
       };
       uint8_t brightness; // 0x01 - 0x64 (1-100)
       uint8_t musicmode_h; // For music mode, 0xff. 0x00 otherwise
       uint8_t musicmode_l; // for music mode, 0x13. 0x00 otherwise. unknown what other values do
       uint8_t mic_sense; // 0x01-0x09 microphone sensitivity 
       uint8_t   twinkle; // twinkle off/on (0x00, 0xff)
       uint8_t   t_speed; // twinkle speed 0x01-0x04
       uint8_t footer [5] =  { 0xff,0,5,0,0xaa }; // trailing bytes. Always 0xff000500aa
     } chardata;

     void init() {
       chardata.header = 0xa5;
       chardata.power = 0;
       chardata.mode = 1;
       chardata.function = 0;
       chardata.fspeed = 8;
       chardata.red = 0xff;
       chardata.green = 0xff;
       chardata.blue = 0xff;
       chardata.filler = 0x00;
       chardata.brightness = 0x64;
       chardata.musicmode_h = 0;
       chardata.musicmode_l = 0;
       chardata.mic_sense = 0x09;
       chardata.twinkle = 0;
       chardata.t_speed = 0x01;
     }

     void write_device_() {
       if (connected() && myRemoteCharacteristic != nullptr) {
        uint8_t* bytes = (uint8_t *)&chardata;
         myRemoteCharacteristic->writeValue(bytes, sizeof(chardata), false);
       }
     }

     
  
   public:

   ChinlyLightDevice() {
     connectionState = new ConnectionState();
     init();
   }

   void setDevice(BLEAdvertisedDevice* device) {
     myDevice = device;
   }

   BLEAdvertisedDevice* getDevice() {
     return myDevice;
   }

   bool connected() {
     return connectionState->connected;
   }

   /*  Connect to the remote device, query for its serviceUUID and if it matches
    *  query for the desired characterisic handle. If any query fails to match, 
    *  drop connection. 
    *  
    *  setDevice must already have been called with a device retrieved from a scan
    *  
    *  Returns true on successfully fetching char handle, false otherwise
    */
   bool connect() {
     if (connected())
       return true;
     if (myDevice == nullptr)
       return false;

     // 3 steps to connecting: connect to device, get service, get remote characteristic handle
     myClient = BLEDevice::createClient();
     myClient->setClientCallbacks(connectionState);
     myClient->connect(myDevice);
     
     myRemoteService = myClient->getService(serviceUUID);
     if (myRemoteService == nullptr) {
       myClient->disconnect();
       return false;
     }

     // Obtain a reference to the characteristic in the service of the remote BLE server.
     myRemoteCharacteristic = myRemoteService->getCharacteristic(charUUID);
     if (myRemoteCharacteristic == nullptr) {
       myClient->disconnect();
       return false;
     }
     return true;
   }

    /* Set power state
     */
    void setState(bool state) {
      if (! connected() )
        return;
      if (state) {
        chardata.power = 0xff;
      }
      else {
        chardata.power = 0x00;
      }
      write_device_();
    }

    bool getState() { 
      return (chardata.power == 0xff);
    }

    // getColor - returns color as an 0xRRGGBBWW hex value. WW is always 00
    uint32_t getColor() {
      return (chardata.color);
    }

    // Set color. Input is in the 0xRRGGBBWW. White is ignored.
    void setColor(uint32_t color) {
      chardata.color = color;
      write_device_();
    }

    uint8_t getBrightness() {
      return (chardata.brightness);
    }

    void setBrightness(uint8_t brightness) {
      if (! connected() )
        return;
      if (brightness > 0x64) {
        brightness = 0x64;
      }
      chardata.brightness = brightness;
      write_device_();
    }

    uint8_t getTwinkleSpeed() {
      if (chardata.twinkle == 0)
        return 0;
      return (chardata.t_speed);
    }

    void setTwinkleSpeed(uint8_t speed) {
      if (! connected() )
        return;
      if (speed > 0) {
        chardata.twinkle = 0xff;
        if (speed > 4)
          speed = 4;
        chardata.t_speed = speed;
      }
      else {
        chardata.twinkle = 0x00;
      }
      write_device_();
    }

    void setMusicMode(uint8_t mode) {
      if (! connected() )
        return;
      if (mode > 0) {
        chardata.mode = 0x04;
        if (mode > 9)
          mode = 4;
        chardata.mic_sense = mode;
        chardata.musicmode_h = 0xff;
        chardata.musicmode_l = 0x13;
      }
      else {
        chardata.mode = 0x01;
      }
      write_device_();
    }
 }; // End of Class

#endif