#include <Arduino.h>
/**
   A BLE client for the Xiaomi Mi Plant Sensor, pushing measurements to an MQTT server.

   See https://github.com/nkolban/esp32-snippets/blob/master/Documentation/BLE%20C%2B%2B%20Guide.pdf
   on how bluetooth low energy and the library used are working.

   See https://github.com/ChrisScheffler/miflora/wiki/The-Basics for details on how the 
   protocol is working.
   
   MIT License

   Copyright (c) 2017 Sven Henkel
   Multiple units reading by Grega Lebar 2018

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
*/

#include <BLEDevice.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include "PubSubClient.h"
#include "config.h"

// device count
static const int deviceCount = sizeof FLORA_DEVICES / sizeof FLORA_DEVICES[0];

// the remote service we wish to connect to
static const BLEUUID serviceUUID("00001204-0000-1000-8000-00805f9b34fb");

// the characteristic of the remote service we are interested in
static const BLEUUID uuid_version_battery("00001a02-0000-1000-8000-00805f9b34fb");
static const BLEUUID uuid_sensor_data("00001a01-0000-1000-8000-00805f9b34fb");
static const BLEUUID uuid_write_mode("00001a00-0000-1000-8000-00805f9b34fb");

// the device topic
static const String deviceBaseTopic = String(MQTT_BASE_TOPIC) + "/" + String(DEVICE_ID) + "/device";

// the base sensor topic
static const String sensorBaseTopic = String(MQTT_BASE_TOPIC) + "/" + String(DEVICE_ID) + "/sensor";

// the capacity of the device json document
static const size_t deviceCapacity = JSON_OBJECT_SIZE(5) + 80;

// the capacity of the sensor json document
static const size_t sensorCapacity = JSON_OBJECT_SIZE(10) + 140;

// boot count used to check if battery status should be read
RTC_DATA_ATTR int bootCount = 0;

TaskHandle_t hibernateTaskHandle = NULL;

WiFiClient espClient;
PubSubClient client(espClient);

void connectWifi(ArduinoJson::JsonDocument &jsonDocument)
{
  Serial.println("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected\n");

  jsonDocument["id"] = DEVICE_ID;
  jsonDocument["ipaddress"] = WiFi.localIP().toString();
  jsonDocument["mac"] = WiFi.macAddress();
  jsonDocument["channel"] = WiFi.channel();
  jsonDocument["rssi"] = WiFi.RSSI();
}

void disconnectWifi()
{
  WiFi.disconnect(true);
  Serial.println("WiFi disonnected");
}

void connectMqtt()
{
  Serial.println("Connecting to MQTT...");
  client.setServer(MQTT_HOST, MQTT_PORT);
  String lwtTopic = deviceBaseTopic + "/lwt";
  String deviceMac = WiFi.macAddress();
  String part = deviceMac.substring(9);
  part.replace(":", "");
  String mqttClientId = String(DEVICE_ID) + "_" + part;

  Serial.printf("- MQTT Client ID: %s\n", mqttClientId.c_str());
  
  while (!client.connected())
  {
    if (!client.connect(mqttClientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD, lwtTopic.c_str(), 1, true, "offline"))
    {
      Serial.print("MQTT connection failed:");
      Serial.print(client.state());
      Serial.println("Retrying...");
      delay(MQTT_RETRY_WAIT);
    }
    else
    {
      client.publish(lwtTopic.c_str(), "online", MQTT_RETAIN);
    }
  }
  Serial.println("MQTT connected");
  Serial.println("");
}

void disconnectMqtt()
{
  client.publish(String(deviceBaseTopic + "/lwt").c_str(), "offline", MQTT_RETAIN);
  client.disconnect();
  Serial.println("MQTT disconnected");
}

BLEClient *getFloraClient(BLEAddress floraAddress)
{
  BLEClient *floraClient = BLEDevice::createClient();

  if (!floraClient->connect(floraAddress))
  {
    Serial.println("- Connection failed, skipping");
    return nullptr;
  }

  Serial.println("- Connection successful");
  return floraClient;
}

BLERemoteService *getFloraService(BLEClient *floraClient)
{
  BLERemoteService *floraService = nullptr;

  try
  {
    floraService = floraClient->getService(serviceUUID);
  }
  catch (...)
  {
    // something went wrong
  }
  if (floraService == nullptr)
  {
    Serial.println("- Failed to find data service");
  }
  else
  {
    Serial.println("- Found data service");
  }

  return floraService;
}

bool forceFloraServiceDataMode(BLERemoteService *floraService)
{
  BLERemoteCharacteristic *floraCharacteristic;

  // get device mode characteristic, needs to be changed to read data
  Serial.println("- Force device in data mode");
  floraCharacteristic = nullptr;
  try
  {
    floraCharacteristic = floraService->getCharacteristic(uuid_write_mode);
  }
  catch (...)
  {
    // something went wrong
  }
  if (floraCharacteristic == nullptr)
  {
    Serial.println("-- Failed, skipping device");
    return false;
  }

  // write the magic data
  uint8_t buf[2] = {0xA0, 0x1F};
  floraCharacteristic->writeValue(buf, 2, true);

  delay(500);
  return true;
}

bool readFloraDataCharacteristic(BLERemoteService *floraService, ArduinoJson::JsonDocument &jsonDocument)
{
  BLERemoteCharacteristic *floraCharacteristic = nullptr;

  // get the main device data characteristic
  Serial.println("- Access characteristic from device");
  try
  {
    floraCharacteristic = floraService->getCharacteristic(uuid_sensor_data);
  }
  catch (...)
  {
    // something went wrong
  }
  if (floraCharacteristic == nullptr)
  {
    Serial.println("-- Failed, skipping device");
    return false;
  }

  // read characteristic value
  Serial.println("- Read value from characteristic");
  std::string value;
  try
  {
    value = floraCharacteristic->readValue();
  }
  catch (...)
  {
    // something went wrong
    Serial.println("-- Failed, skipping device");
    return false;
  }
  const char *val = value.c_str();

  Serial.print("Hex: ");
  for (int i = 0; i < 16; i++)
  {
    Serial.print((int)val[i], HEX);
    Serial.print(" ");
  }
  Serial.println(" ");

  int16_t *temp_raw = (int16_t *)val;
  float temperature = (*temp_raw) / ((float)10.0);
  Serial.printf("-- Temperature: %f\n", temperature);

  int moisture = val[7];
  Serial.printf("-- Moisture: %d\n", moisture);

  int light = val[3] + val[4] * 256;
  Serial.printf("-- Light: %d\n", light);

  int conductivity = val[8] + val[9] * 256;
  Serial.printf("-- Conductivity: %d\n", conductivity);

  if (temperature < -20 || temperature > 50)
  {
    Serial.println("-- Unreasonable values received for temperature, skip publish");
    return false;
  }

  if (moisture < 0 || moisture > 100)
  {
    Serial.println("-- Unreasonable values received for moisture, skip publish");
    return false;
  }

  if (light < 0)
  {
    Serial.println("-- Unreasonable values received for light, skip publish");
    return false;
  }

  if (conductivity < 0 || conductivity > 5000)
  {
    Serial.println("-- Unreasonable values received for conductivity, skip publish");
    return false;
  }

  // add all valid measurment values to the json
  jsonDocument["temperature"] = temperature;
  jsonDocument["moisture"] = moisture;
  jsonDocument["light"] = light;
  jsonDocument["conductivity"] = conductivity;

  return true;
}

bool readFloraBatteryCharacteristic(BLERemoteService *floraService, ArduinoJson::JsonDocument &jsonDocument)
{
  BLERemoteCharacteristic *floraCharacteristic = nullptr;

  // get the device battery characteristic
  Serial.println("- Access battery characteristic from device");
  try
  {
    floraCharacteristic = floraService->getCharacteristic(uuid_version_battery);
  }
  catch (...)
  {
    // something went wrong
  }
  if (floraCharacteristic == nullptr)
  {
    Serial.println("-- Failed, skipping battery level");
    return false;
  }

  // read characteristic value
  Serial.println("- Read value from characteristic");
  std::string value;
  try
  {
    value = floraCharacteristic->readValue();
  }
  catch (...)
  {
    // something went wrong
    Serial.println("-- Failed, skipping battery level");
    return false;
  }
  const char *val2 = value.c_str();
  int battery = val2[0];

  jsonDocument["battery"] = battery;
  jsonDocument["batterylow"] = battery <= BATTERY_THRESHOLD;

  return true;
}

bool processFloraService(BLERemoteService *floraService, const char *deviceMacAddress, bool readBattery, ArduinoJson::JsonDocument &jsonDocument)
{
  // set device in data mode
  if (!forceFloraServiceDataMode(floraService))
  {
    return false;
  }

  bool dataSuccess = readFloraDataCharacteristic(floraService, jsonDocument);

  bool batterySuccess = true;
  if (readBattery)
  {
    batterySuccess = readFloraBatteryCharacteristic(floraService, jsonDocument);
  }

  return dataSuccess && batterySuccess;
}

bool processFloraDevice(BLEAddress floraAddress, const char *deviceMacAddress, bool getBattery, int tryCount, ArduinoJson::JsonDocument &jsonDocument)
{
  Serial.print("Processing Flora device at ");
  Serial.print(floraAddress.toString().c_str());
  Serial.print(" (try ");
  Serial.print(tryCount);
  Serial.println(")");

  // connect to flora ble server
  BLEClient *floraClient = getFloraClient(floraAddress);
  if (floraClient == nullptr)
  {
    return false;
  }

  // connect data service
  BLERemoteService *floraService = getFloraService(floraClient);
  if (floraService == nullptr)
  {
    floraClient->disconnect();
    return false;
  }

  // process devices data
  bool success = processFloraService(floraService, deviceMacAddress, getBattery, jsonDocument);

  // disconnect from device
  floraClient->disconnect();

  return success;
}

void hibernate()
{
  esp_sleep_enable_timer_wakeup(SLEEP_DURATION * 1000000ll);
  Serial.println("Going to sleep now.");
  delay(100);
  esp_deep_sleep_start();
}

void delayedHibernate(void *parameter)
{
  delay(EMERGENCY_HIBERNATE * 1000); // delay for five minutes
  Serial.println("Something got stuck, entering emergency hibernate...");
  hibernate();
}

void setup()
{
  // all action is done when device is woken up
  Serial.begin(115200);
  delay(1000);

  // increase boot count
  bootCount++;

  // create a hibernate task in case something gets stuck
  xTaskCreate(delayedHibernate, "hibernate", 4096, NULL, 1, &hibernateTaskHandle);

  Serial.println("Initialize BLE client...");
  BLEDevice::init("");
  BLEDevice::setPower(ESP_PWR_LVL_P9);

  // create device Json Document
  DynamicJsonDocument deviceJson(deviceCapacity);

  // connecting wifi and mqtt server
  connectWifi(deviceJson);
  connectMqtt();

  // publish device status
  char payload[deviceCapacity];
  serializeJson(deviceJson, payload);
  String deviceStatusTopic = deviceBaseTopic + "/status";

  Serial.printf("Device status: %s\n", payload);
  if (client.publish(deviceStatusTopic.c_str(), payload, MQTT_RETAIN))
  {
    Serial.printf("- Publishing device status -> %s\n", deviceStatusTopic.c_str());
  }
  else
  {
    Serial.println("- Publishing device status failed!");
  }

  // check if battery status should be read - based on boot count
  bool readBattery = ((bootCount % BATTERY_INTERVAL) == 0);

  // process devices
  for (int i = 0; i < deviceCount; i++)
  {
    Serial.println();
    int tryCount = 0;
    String sensorMacAddress = FLORA_DEVICES[i][0];
    String sensorLocation = FLORA_DEVICES[i][1];
    String sensorId = FLORA_DEVICES[i][2];

    // create sensor Json Document
    DynamicJsonDocument sensorJson(sensorCapacity);
    sensorJson["id"] = sensorId;
    sensorJson["location"] = sensorLocation;
    sensorJson["mac"] = sensorMacAddress;

    BLEAddress bleAddress(FLORA_DEVICES[i][0].c_str());
    while (tryCount < RETRY)
    {
      tryCount++;
      sensorJson["retrycount"] = tryCount;

      // create sensor topic
      String sensorTopic = sensorBaseTopic + "/" + sensorLocation + "/" + sensorId;

      if (processFloraDevice(bleAddress, sensorMacAddress.c_str(), readBattery, tryCount, sensorJson))
      {
        char payload[sensorCapacity];
        serializeJson(sensorJson, payload);
        Serial.printf("- Sensor payload: %s\n", payload);
        if (client.publish(sensorTopic.c_str(), payload, MQTT_RETAIN))
        {
          Serial.printf("-- Publishing --> %s\n", sensorTopic.c_str());
        }
        else
        {
          Serial.println("-- Publishing failed!");
        }
        break;
      }
      delay(1000);
    }
    delay(1500);
  }

  // disconnect mqtt and wifi
  disconnectMqtt();
  disconnectWifi();

  // delete emergency hibernate task
  vTaskDelete(hibernateTaskHandle);

  // go to sleep now
  hibernate();
}

void loop()
{
  /// we're not doing anything in the loop, only on device wakeup
  delay(10000);
}