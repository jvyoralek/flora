# Xiaomi Flora Plant sensors [![Build Status](https://travis-ci.com/RaymondMouthaan/miflora-esp32.svg?branch=master)](https://travis-ci.com/github/RaymondMouthaan/miflora-esp32)

This [PlatformIO](https://platformio.org) project implements an ESP32 BLE client for Xiaomi Flora Plant sensors, pushing the measurements in json format to a MQTT broker.

![xiaomi-flora](xiaomi-miflora.png)

## Features

Base on the great work of @sidddy and @jvyoralek (and all other contributors), this project adds:

- Support for multiple Miflora sensors with friendly named topics
- Payloads in json format
- Device (Wifi) status and lwt (last will and testament)
- Seperate configurable (sub) topics
- Measurement levels (configurable by min, max values)
- Battery low status and battery level (configurable by thresholds)

__Note : tested with a maximum of 8 Miflora sensors configured, however the ESP32 for some unknown reason get's stuck sometimes. With 4 Miflora sensors configured the ESP32 looks stable and therefore it's advisable to configure a maximum of 4 Miflora sensors per ESP32.__

## Technical Requirements

Hardware:
- ESP32 device ([ESP32 at AliExpress](https://nl.aliexpress.com/wholesale?catId=0&initiative_id=SB_20200408062838&SearchText=MH-ET+Live+ESP32))
- Xiaomi Flora Plant Sensor (firmware revision >= 2.6.6) ([Xiaomi flora at AliExpress](https://nl.aliexpress.com/wholesale?catId=0&initiative_id=SB_20200408063038&SearchText=xiaomi+flora))

Software:
- MQTT broker (e.g. [Mosquitto](https://mosquitto.org))

## Quick Setup Instructions

Assumed you are familiar with [Visual Studo Code](https://code.visualstudio.com) and [PlatformIO](https://platformio.org). 

1) Open the project in Visual Studio Code with PlatformIO installed
2) Copy `example/config.h.example` to `include/config.h` and update settings according to your environment:
    - MAC address(es), location(s), plant id(s) and min and max values of your Xiaomi Flora Plant sensor(s)
    - Device id
    - WLAN Settings
    - MQTT Settings
3) Modify platform.ini according to your environment:
    - upload_port
    - monitor_port

## Measuring Interval

The ESP32 will perform a single connection attempt to the Xiaomi Mi Plant sensor, read the sensor data & push it to the MQTT server. The ESP32 will enter deep sleep mode after all sensors have been read and sleep for n minutes before repeating the exercise...
Battery level is read every nth wakeup.
Up to n attempst per sensor are performed when reading the data fails.

## Configuration

- `SLEEP_DURATION` - how long should the device sleep between sensor reads?
- `EMERGENCY_HIBERNATE` - how long after wakeup should the device forcefully go to sleep (e.g. when something gets stuck)?
- `BATTERY_INTERVAL` - how often should the battery status be read?
- `BATTERY_THRESHOLD` - when is the battery on low power?
- `SENSOR_RETRY` - how ofter should a single sensor be tried on each run?

## Topics

The ESP32 will publish payload in json format on two base topics and are divided in two categories:

### Device topic

This topic is used to publish payload related to the esp32 device status and lwt (last will and testament).

#### Last will and testament

When the ESP32 connects to the MQTT broker a `online` message will be published to the `/device/lwt` subtopic to indicate the ESP32 is online.

Topic format: `<base_topic>/<device_id>/device/lwt`

Example plain text payload:
```
online
```

When the ESP32 disconnects from the MQTT broker an `offline` message is published to the `/device/lwt` subtopic to indicate the ESP32 is offline and in sleep mode.

Example plain text payload:
```
offline
```

#### Status

The subtopic `/device/status` is used to publish payload related to ESP32 (WiFi) status.

Topic format: `<base_topic>/<device_id>/device/status`

Example json payload:
```
{
    "id": "esp32-1",
    "ipaddress": "10.40.1.1",
    "mac": "24:62:AB:CA:F6:40",
    "channel": 10,
    "rssi": -51
}
```

 - `id` - the id of the ESP32 as defined in `config.h`
 - `ipaddress` - the ip address of the ESP32 given by the DHCP server
 - `mac` - the MAC address of the ESP32
 - `channel` - the channel of the WiFi network
 - `rssi` - the WiFi Received Signal Strength Indicator

### Sensor topic

This topic is used to publish payload related to the Miflora sensor measurements. Each Miflora sensor has its own subtopic. 

Topic format: `<base_topic>/<device_id>/sensor/<location>/<plant_id>`

Example json payload:
```
{
    "id": "calathea-1",
    "location": "livingroom",
    "mac": "C4:7C:8D:67:57:07",
    "retryCount": 1,
    "rssi": -75,
    "temperature": 21.2,
    "temperatureLevel": 1,
    "moisture": 4,
    "moistureLevel": 0,
    "light": 1203,
    "lightLevel": 1,
    "conductivity": 72,
    "conductivityLevel": 0,
    "battery": 98,
    "batteryLow": false,
    "batteryLevel": 2
}
```

- `id` - the id of the plant
- `location` - the location where the Miflora is placed
- `mac` - the MAC address of the Miflora
- `retryCount` - number of retry attempts to retreive valid data from the Miflora
- `rssi` - the BLE Received Signal Strength Indicator
- `temperature` - the measured temperature in degree Celsius
- `temperatureLevel` - indicates if the temperature is 0=low, 1=medium or 2=high, configurable by minTemperature and maxTemperature
- `moisture` - the measured moisture in percentage
- `moistureLevel` - indicates if the moisture is 0=low, 1=medium or 2=high, configurable by minMoisture and maxMoisture
- `light` - the measured light in lux
- `lightLevel` - indicates if the light is 0=low, 1=medium or 2=high, configurable by minLight and maxLight
- `conductivity` - the measured conductivity in uS/cm
- `conductivityLevel` - indicates if the conductivity is 0=low, 1=medium or 2=high, configurable by minConductivity and maxConductivity
- `battery` - the measured battery level in percentage
- `batteryLow` - indicates if the battery is low (true or false), based on the battery low threshold
- `batteryLevel` - indicates if the battery is 0=low, 1=medium or 2=high, based on the battery low and mediun thresholds

__Note: the min and max values for each measurement can be found in the Flower Care app for [IOS](https://apps.apple.com/us/app/flower-care/id1095274672)__ or [Android](https://play.google.com/store/apps/details?id=com.huahuacaocao.flowercare&hl=en).

## Constraints

Some "nice to have" features are not yet implemented or cannot be implemented:
  - OTA updates: I didn't manage to implement OTA update capabilities due to program size constraints: BLE and WLAN brings the sketch up to 90% of the size limit, so I decided to use the remaining 10% for something more useful than OTA...

## Sketch size issues

The sketch does not fit into the default arduino parition size of around 1.3MB. You'll need to change your default parition table and increase maximum build size to at least 1.6MB.
On Arduino IDE this can be achieved using "Tools -> Partition Scheme -> No OTA". 
For this platform.io project this is achieved using `board_build.partitions = no_ota.csv` in `platformio.ini`.

## Credits
Many thanks go to @sidddy and @jvyoralek for most of the work of the project and improvements!
Many thanks go to the guys at https://github.com/open-homeautomation/miflora for figuring out the sensor protocol.
