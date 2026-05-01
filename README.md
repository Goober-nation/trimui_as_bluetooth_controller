# Use your Trimui Smart Pro as a Bluetooth controller

Simply unpack the zip into the apps directory on the SD card.

Works by manually awaking the Bluetooth module and configuring the HID.

## Shortcuts
_Menu+Start_ - quit the app

_Menu+L1_ - shortcut for L3

_Menu+R1_ - shortcut for R3

_Menu+Y_ - actual press of the menu button is received on the side of the device the "controller" is connected to

## Instructions for compilation

Here is how i compiled the script. Be careful with these instructions, they worked for me and CrossMix OS 1.0.4.

### 1. Create a local 'sysroot' folder for the ARM headers and libraries
```
mkdir -p sysroot
```

### 2. Download the exact Noble (24.04) ARMHF packages using wget wildcards
```
wget -r -l1 -nd -A 'libbluetooth3_*_armhf.deb' http://ports.ubuntu.com/pool/main/b/bluez/
wget -r -l1 -nd -A 'libbluetooth-dev_*_armhf.deb' http://ports.ubuntu.com/pool/main/b/bluez/
```

### 3. Extract the contents directly into the sysroot folder
```
dpkg -x libbluetooth3_*_armhf.deb ./sysroot
dpkg -x libbluetooth-dev_*_armhf.deb ./sysroot
```

### 4. Clean up the downloaded .deb installers
```
rm *.deb
sudo dpkg --remove-architecture armhf
sudo apt update
sudo dpkg --add-architecture armhf
sudo apt update
sudo apt install libbluetooth-dev:armhf
```

### 5. Compile the script
```
arm-linux-gnueabihf-gcc hid_server.c -o hid_server \
    --sysroot=./sysroot \
    -I./sysroot/usr/include \
    -L./sysroot/usr/lib/arm-linux-gnueabihf \
    -static -lbluetooth
```
