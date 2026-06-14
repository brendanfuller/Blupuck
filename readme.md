> Notice: Project was built with Claude. 

# Blupuck

Connect your wireless game controllers to a PC through a Raspberry Pi Pico W.
Blupuck pairs controllers over Bluetooth and makes them show up on your computer
as standard Xbox controllers. A simple web page lets you
manage everything from your browser which you can view here

> Web Interface to manage: https://blupuck.brendanfuller.com

Project inspired by the [Steam Controller](https://store.steampowered.com/sale/steamcontroller) and it's wireless puck.

## Features

- Makes your controller appear as an Xbox controller on your PC. No drivers to
  install.
- Rumble works.
- Mouse mode: hold a button and use a stick to move the mouse, with click on A
  and B. Handy for navigating the desktop between games.
- Wakes your PC when you turn on a controller.
- Press the button on the Pico to start pairing a new controller.
- Remembers your controllers and reconnects to them automatically.

## Controller support

Blupuck works with most popular wireless controllers, including:

- Nintendo Switch Pro Controller and Joy-Cons
- Xbox One and Xbox Series controllers
- PlayStation DualShock 4 (PS4), DualSense (PS5), and DualShock 3 (PS3)
- Nintendo Wii and Wii U
- Google Stadia
- 8BitDo controllers
- Many generic Android Bluetooth gamepads

Controllers are handled by the Bluepad32 project. For the full, up-to-date list,
see: https://bluepad32.readthedocs.io/en/latest/supported_gamepads/

## Limitations/WIP

- One controller at a time is the reliable limit today. The design can hold up
  to four slots, but more than one connection is unstable on the Pico W's
  Bluetooth radio. Improving this is a work in progress.
- Switch Pro Controllers run without motion (gyro) data to keep things smooth.
  Buttons, sticks, and triggers all work.

## What you need

- A Raspberry Pi Pico W or Raspberry Pi Pico W 2

### Build and flash the firmware

```
export PICO_SDK_PATH=/path/to/pico-sdk
cd firmware
git submodule update --init --recursive
mkdir build && cd build
cmake ..
make -j
```

Hold the BOOTSEL button while plugging in the Pico W, then copy
`firmware/build/bridge.uf2` onto the drive that appears.

### Run the web page

```
cd webui
npm install
npm run dev
```

Open the link it prints, click Connect, and pick Blupuck from the list.


## Credits

Built on Bluepad32, BTstack, TinyUSB, and the Raspberry Pi Pico SDK.


## License
MIT