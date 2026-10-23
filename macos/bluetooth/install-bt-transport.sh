#!/bin/sh
# Make macOS's Bluetooth daemon look for a USB controller (BT_HCI_TRANSPORT=USB) at every boot.
# bluetoothd otherwise picks the transport from the board id of the Mac it thinks it is (MacBookPro16,2: UART).
# Undo: sudo macos/bluetooth/uninstall-bt-transport.sh
set -e
P=/Library/LaunchDaemons/local.xe.bt-transport.plist
cat > "$P" <<'PL'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>local.xe.bt-transport</string>
  <key>ProgramArguments</key><array><string>/bin/launchctl</string><string>setenv</string><string>BT_HCI_TRANSPORT</string><string>USB</string></array>
  <key>RunAtLoad</key><true/>
</dict></plist>
PL
chown root:wheel "$P"; chmod 644 "$P"
plutil -lint "$P"
launchctl bootout system "$P" 2>/dev/null || true
launchctl bootstrap system "$P"
launchctl setenv BT_HCI_TRANSPORT USB
echo "installed: $P (also set for the running system now)"
