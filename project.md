## Solar-powered e-paper environmental sensor node

A small autonomous outdoor sensor/display built around an **ESP32-C3**, powered by a **solar panel + supercapacitor**, with an **e-paper display** that keeps showing information without consuming power.

### Goal

Create a low-maintenance device that can:

* measure environmental conditions,
* display data continuously,
* run from harvested solar energy,
* survive periods without sunlight.

---

## System architecture

```
              Sun
               |
               v
        5V Solar panel
        (~1 W / 200 mA)
               |
               |
       Schottky diode
      (reverse protection)
               |
               v
        5.5V 4F Supercap
               |
               v
       Low-power 3.3V supply
               |
               v
          ESP32-C3
          /      \
         /        \
   Sensors       E-paper
```

---

## Main components

### Controller

**ESP32-C3 SuperMini**

Role:

* reads sensors,
* updates display,
* manages sleep modes,
* optionally connects via WiFi/BLE.

Why:

* low power,
* small,
* modern replacement for ESP8266/Wemos D1 Mini.

---

### Display

**2.9" e-paper display**

Recommended:

* black/white version for lowest power

Features:

* SPI interface
* readable in sunlight
* image remains without power
* only consumes power during refresh

Example display:

```
Outdoor Monitor

Temperature
21.8 °C

Humidity
56 %

Pressure
1015 hPa

Light
8200 lux
```

---

### Energy system

#### Solar panel

Recommended:

* 5 V
* ~200 mA
* ~1 W

Purpose:

* charges capacitor during daylight.

#### Storage

Existing:

* **5.5 V 4 F supercapacitor**

Energy:

≈ 60 joules
≈ 17 mWh

Enough for:

* many e-paper refresh cycles,
* hours of operation during no sunlight (depending on sleep strategy).

#### Protection

Schottky diode:

* 1N5819 or 1N5822

Purpose:

* prevents capacitor discharging into the solar panel at night.

---

## Sensors

Recommended sensor set:

### BME280

Measures:

* temperature
* humidity
* air pressure

Good for:

* weather display
* trend detection

### BH1750

Measures:

* light intensity (lux)

Useful for:

* displaying sunlight level,
* optimizing solar operation.

Optional:

### DHT11 / DHT20

Simple temperature/humidity sensor.

### BME680

Adds:

* VOC / air quality

but uses more power.

---

## Software concept

Normal operation:

```
Wake up
   |
Read sensors
   |
Update e-paper
   |
Store values
   |
Deep sleep
   |
Repeat
```

Example:

* Update every 5 minutes
* ESP32 sleeps between measurements
* e-paper stays visible without power

---

## Expected power behavior

Typical:

### During measurement

* ESP32: tens of mA
* sensors: few mA
* e-paper refresh: short burst

### Sleeping

* ESP32-C3: µA range possible
* e-paper: almost zero

This makes solar operation realistic.

---

## Suggested first prototype BOM

| Part         | Choice                             |
| ------------ | ---------------------------------- |
| MCU          | ESP32-C3 SuperMini                 |
| Display      | 2.9" e-paper                       |
| Storage      | 5.5 V 4 F supercapacitor           |
| Solar        | 5 V 200 mA panel                   |
| Regulator    | low-Iq 3.3 V regulator             |
| Sensor       | BME280                             |
| Light sensor | BH1750                             |
| Protection   | 1N5819 Schottky diode              |
| Wiring       | 20 AWG power, thinner signal wires |

---

## Possible extensions

* WiFi weather upload
* MQTT home automation node
* LoRa remote sensor
* rain gauge
* soil moisture monitoring
* solar charging monitor
* wildlife/environment monitoring station

Overall, this is a realistic low-power embedded project. The combination of **e-paper + ESP32-C3 + solar + supercapacitor** is particularly well matched because the display does not require continuous power.
