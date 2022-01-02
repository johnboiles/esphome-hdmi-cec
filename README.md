# HDMI-CEC ESPHome Component

An ESPHome component that supports receiving and transmitting HDMI-CEC messages to connected HDMI devices.

It's working, but very rough around the edges. Specifically it doesn't work well on ESP8266 when WiFi is enabled. See the notes section below. The ultimate goal of this project is to eventually be merged into the core ESPHome project once it's up to quality.

The core CEC driver is forked from https://github.com/arpruss/ArduinoLib_CEClient.

My use case: I already have an IR blaster built with ESPHome, but my new TCL TV has a Bluetooth remote. I want to control my older sound gear (connected over optical) with the TV remote. This component allows me to intercept HDMI-CEC volume commands and transmit the IR codes to control the soundbar. Theoretically it should allow you to make any older non-HDMI equipment work seamlessly with newer gear. You can also do things like monitor which source is selected, which HDMI devices are powered on, etc.

Drop me a line on [Twitter](http://twitter.com/johnboiles) if you end up trying it and/or have ideas on cool use cases!

## Electronics

Since HDMI-CEC is a 3.3V protocol, no external components are needed. A [HDMI breakout connector](https://www.amazon.com/gp/product/B075Q8HG2B) might be handy though. Just connect:

* GPIO of your choice to HDMI pin 13 (the examples use GPIO4)
* GND from the ESP board to HDMI pin 17 (DDC/CEC Ground)
* 5V from the ESP board to HDMI pin 18 (typtically how the HDMI device knows something is connected)

## Usage

For now on the ESP8266, until the HDMI driver is rewritten to use interrupts, it's a good idea to bump the CPU speed to 160MHz:

```yaml
esphome:
  ...
  platformio_options:
    board_build.f_cpu: 160000000L
```

Add this repo to your external components

```yaml
external_components:
  - source: github://ohnboiles/esphome-hdmi-cec
```

Configure your `hdmi_cec` component. The `on_message` triggers will only fire if any specified `source`, `destination`, `opcode`, and `data` match.

```yaml
hdmi_cec:
  address: 0x05 # Audio System
  pin: 4 # GPIO4
  on_message:
    - opcode: 0xC3 # Request ARC start
      then:
        - hdmi_cec.send: # Report ARC started
            destination: 0x0
            data: [ 0xC1 ]
    - opcode: 0x71 # Give audio status
      source: 0x0 # From the TV
      destination: 0x5 # To our address
      then:
        - hdmi_cec.send:
            destination: 0x0
            data: [ 0x00, 0x71, 0x04 ]
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

* The timing for receiving and parsing CEC messages depends on the timing with which `loop` is called and thus this is very sensitive to other things the microcontroller are doing that may delay the `loop` method getting called by > 0.1ms. In my testing it only works really reliably on an ESP8266 with WiFi disabled. This will be fixed by better use of interrupts in the HDMI-CEC driver. The ESP32 will probably work better as-is, though I haven't tried it yet.

## See Also

* I originally prototyped something like this with a Raspberry Pi. [Here's my writeup](https://community.home-assistant.io/t/cec-volume-control-for-ir-devices-by-pretending-to-be-an-hdmi-arc-device/323047/7) on that.
* [CEC-O-MATIC](https://www.cec-o-matic.com) is absurdly useful for parsing CEC messages.
* [HDMI 1.3a Spec](https://web.archive.org/web/20171009194844/http://www.microprocessor.org/HDMISpecification13a.pdf)
* https://github.com/s-moch/CEC - An ESP8266-specific CEC driver that I'll probably move towards (also based on [https://github.com/arpruss/ArduinoLib_CEClient](arpruss/ArduinoLib_CEClient)). It's not working for me yet, and it doesn't support interrupts, but it's a much simpler codebase to start from.
* [Nuvoton HDMI-CEC application note](https://www.nuvoton.com/export/resource-files/AN_0004_HDMI-CEC_EN_Rev1.00.pdf)
* [NXP HDMI-CEC application note](https://www.nxp.com/docs/en/application-note/AN12732.pdf)
