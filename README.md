# Xiaomi Mi Flora Plant sensors

This [Platform.io](https://platformio.org) project implements an ESP32 BLE client for Xiaomi Mi Flora Plant sensors, pushing the measurements in json format to a MQTT broker.

## Features
Base on the great work of @sidddy and @jvyoralek (and all other contributors), this project adds:

- Support for multiple Miflora sensors with friendly named topics
- Payloads in json format
- Device (Wifi) status and lwt (last will and testament)
- Seperate configurable (sub) topics
- Battery low status (configurable by threshold)

## Technical requirements

Hardware:
- ESP32 device
- Xiaomi Mi Plant Sensor (firmware revision >= 2.6.6)

Software:
- MQTT broker (e.g. [Mosquitto](https://mosquitto.org))

## Quick setup instructions
Assumed you are familiar with Visual Code and PlatformIO. 

1) Open the project in Visual Code with PlatformIO installed
2) Copy `example/config.h.example` to `include/config.h` and update settings according to your environment:
    - MAC address(es), location(s) and plant id(s) of your Xiaomi Mi Plant sensor(s)
    - Device id
    - WLAN Settings
    - MQTT Settings
3) Modify platform.ini according to your environment:
    - upload_port
    - monitor_port

## Measuring interval

The ESP32 will perform a single connection attempt to the Xiaomi Mi Plant sensor, read the sensor data & push it to the MQTT server. The ESP32 will enter deep sleep mode after all sensors have been read and sleep for n minutes before repeating the exercise...
Battery level is read every nth wakeup.
Up to n attempst per sensor are performed when reading the data fails.

## Configuration

- `SLEEP_DURATION` - how long should the device sleep between sensor reads?
- `EMERGENCY_HIBERNATE` - how long after wakeup should the device forcefully go to sleep (e.g. when something gets stuck)?
- `BATTERY_INTERVAL` - how often should the battery status be read?
- `BATTERY_THRESHOLD` - when is the battery on low power?
- `RETRY` - how ofter should a single device be tried on each run?

## Topics

The ESP32 will publish payload in json format on two base topics and are divided in two cathegories:

### Device topic
This topic is used to publish payload related to the esp32 device status and lwt (last will and testament).

#### Last Will and Testament
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
 - `rssi` - Received Signal Strength Indicator

### Sensor topic
This topic is used to publish payload related to the Miflora sensor measurements. Each Miflora sensor has its own subtopic. 

Topic format: `<base_topic>/<device_id>/sensor/<location>/<plant_id>`

Example json payload:
```
{
    "id": "calathea-1",
    "location": "livingroom",
    "mac": "C4:7C:8D:67:57:07",
    "retrycount": 1,
    "temperature": 21,
    "moisture": 3,
    "light": 2117,
    "conductivity": 10,
    "battery": 98,
    "batterylow": false
}
```

- `id` - the id of the plant as defined in `config.h`
- `location` - the location where the Miflora is placed
- `mac` - the MAC address of the Miflora
- `retrycount` - number of retry attempts to retreive valid data from the Miflora
- `temperature` - the measured temperature in degree Celsius
- `moisture` - the measured moisture in percentage
- `light` - the measured light in lux
- `conductivity` - the measured conductivity in uS/cm
- `battery` - the measured battery level in percentage
- `batterylow` - indicates if the battery is low, based on the battery treshold defined in `config.h`

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
