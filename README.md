# HDMI-CEC ESPHome Component

An ESPHome component that supports receiving and transmitting HDMI-CEC messages to connected HDMI devices. The ultimate goal of this project is to eventually be merged into the core ESPHome project once it's up to quality. It's currently out for review as [esphome/esphome#3017](https://github.com/esphome/esphome/pull/3017/) (see also: [esphome/esphome-docs#1789](https://github.com/esphome/esphome-docs/pull/1789)).

The core CEC driver is [forked](https://github.com/johnboiles/CEC) from [s-moch/CEC](https://github.com/s-moch/CEC).

My use case: I already have an IR blaster built with ESPHome, but my new TCL TV has a Bluetooth remote. I want to control my older sound gear (connected over optical) with the TV remote but this TV only supports controlling sound gear via HDMI (typically for HDMI-ARC devices). This component allows me to intercept HDMI-CEC volume commands and transmit the IR codes to control the soundbar. It also allows my Apple TV to control the soundbar (so I can control volume from the Remote app on my phone 📱). Theoretically it should allow you to make any older non-HDMI equipment work seamlessly with newer gear. You can also do things like monitor which source is selected, which HDMI devices are powered on, etc.

Drop me a line on [Twitter](http://twitter.com/johnboiles) or the [ESPHome Discord](https://discord.gg/KhAMKrd) if you end up trying it and/or have ideas for cool use cases!

## Electronics

Since HDMI-CEC is a 3.3V protocol, no external components are needed. A [HDMI breakout connector](https://www.amazon.com/gp/product/B075Q8HG2B) might be handy though. Just connect:

* GPIO of your choice to HDMI pin 13 (the examples use GPIO4)
* GND from the ESP board to HDMI pin 17 (DDC/CEC Ground)
* 5V from the ESP board to HDMI pin 18 (typtically how the HDMI device knows something is connected)

## Usage

For now on the ESP8266, until the HDMI driver is rewritten to fully use interrupts, it's a good idea to bump the CPU speed to 160MHz:

```yaml
esphome:
  ...
  platformio_options:
    board_build.f_cpu: 160000000L
```

Add this repo to your external components

```yaml
external_components:
  - source: github://johnboiles/esphome-hdmi-cec
```

Configure your `hdmi_cec` component. The `on_message` triggers will only fire if any specified `source`, `destination`, `opcode`, and `data` match. Something like this (the below is abbreviated -- see full example [here](https://github.com/johnboiles/esphome-hdmi-cec/blob/main/example_fake_arc_device.yaml)).

```yaml
hdmi_cec:
  # The initial logical address -- corresponds to device type. This may be
  # reassigned if there are other devices of the same type on the CEC bus.
  address: 0x05 # Audio system
  # Promiscuous mode can be enabled to allow receiving messages not intended for us
  promiscuous_mode: false
  # Typically the physical address is discovered based on the point-to-point
  # topology of the HDMI connections using the DDC line. We don't have access
  # to that so we just hardcode a physical address.
  physical_address: 0x4000
  pin: 4 # GPIO4
  on_message:
    - opcode: 0xC3 # Request ARC start
      then:
        - hdmi_cec.send: # Report ARC started
            destination: 0x0
            data: [ 0xC1 ]
    - opcode: 0x71 # Give audio status
      source: 0x0 # From the TV
      then:
        - hdmi_cec.send:
            destination: 0x0
            data: [ 0x7A, 0x7F ]
    - opcode: 0x46 # Give OSD name
      then:
        - hdmi_cec.send:
            destination: 0x0
            data: [0x47, 0x65, 0x73, 0x70, 0x68, 0x6F, 0x6D, 0x65]
    - data: [0x44, 0x41] # User control pressed: volume up
      then:
        - logger.log: "Volume up"
```

You can use the `hmdi_cec.send` action from anywhere you can use actions.

```yaml
button:
  - platform: template
    name: TV Turn On
    on_press:
      - hdmi_cec.send:
          source: 0x05 # Optional since we'll default to `hdmi_cec.address`
          destination: 0x0
          data: [ 0x04 ]
```

## Notes

* The timing for receiving and parsing CEC messages depends on the timing with which `loop` is called and thus this is sensitive to other things the microcontroller are doing that may delay the `loop` method getting called. I've implemented pin change interrupts which makes receive more reliable but more work needs to be done to make this entirely interrupt driven. The ESP32 will probably work better as-is, though I haven't tried it yet.

## TODO

* Adapt or rewrite the CEC driver to be fully interrupt-driven.
* Add GitHub action to test the build
* Write tests?
* Fritzing (or similar) connection diagram

## See Also

* I originally prototyped something like this with a Raspberry Pi. [Here's my writeup](https://community.home-assistant.io/t/cec-volume-control-for-ir-devices-by-pretending-to-be-an-hdmi-arc-device/323047/7) on that.
* [CEC-O-MATIC](https://www.cec-o-matic.com) is absurdly useful for parsing CEC messages.
* [HDMI 1.3a Spec](https://web.archive.org/web/20171009194844/http://www.microprocessor.org/HDMISpecification13a.pdf)
* [Nuvoton HDMI-CEC application note](https://www.nuvoton.com/export/resource-files/AN_0004_HDMI-CEC_EN_Rev1.00.pdf)
* [NXP HDMI-CEC application note](https://www.nxp.com/docs/en/application-note/AN12732.pdf)
