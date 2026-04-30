#!/bin/sh
# TrimUI Native HID - Persistent Agent & Logging Edition

echo "[*] Cleaning up old states..."
killall -9 bluetoothd hid_server bluetoothctl hcidump 2>/dev/null
/etc/init.d/hciattach stop 2>/dev/null
sleep 1

# Clear old logs
> /root/btlog.log
> /root/hcidump.log

echo "[*] Starting Bluetooth Hardware via OS Init..."
/etc/init.d/hciattach start

echo "[*] Waiting for the kernel to expose hci0..."
while ! hciconfig -a | grep -q "hci0"; do
    sleep 0.5
done
echo "[+] hci0 hardware node detected."

echo "[*] Bringing interface UP..."
hciconfig hci0 up
sleep 1

echo "[*] Starting BlueZ Daemon (Pure Pipe Mode)..."
# We add --noplugin to stop BlueZ from hijacking PSM 17/19
/usr/bin/bluetoothd -n -d -C -E --noplugin=input,hog,a2dp,avrcp,network,pan > /root/btlog.log 2>&1 &
sleep 2

echo "[*] Starting packet sniffer (Routing to /root/hcidump.log)..."
hcidump -X > /root/hcidump.log 2>&1 &

echo "[*] Setting hardware class and pairing mode..."
hciconfig hci0 class 0x000508
hciconfig hci0 sspmode 1 

echo "[*] Spawning Persistent Headless Agent..."
# Spoon-feed the commands to prevent skipping, and log the output
(
    echo "power on"
    sleep 0.5
    echo "agent off"
    sleep 0.5
    echo "agent NoInputNoOutput"
    sleep 0.5
    echo "default-agent"
    sleep 0.5
    echo "system-alias 'Trimui_Gamepad'"
    sleep 0.5
    echo "discoverable on"
    sleep 0.5
    echo "pairable on"
    # Keep the pipe open indefinitely so the agent stays alive
    tail -f /dev/null 
) | bluetoothctl > /root/btctl.log 2>&1 &
sleep 2

echo "[*] Injecting HID Service..."
sdptool add HID

echo "[*] Starting Native HID Server..."
if [ -x "./hid_server" ]; then
    ./hid_server
else
    echo "[!] Error: ./hid_server not found or not executable."
fi