#!/bin/sh
P=/Library/LaunchDaemons/local.xe.bt-transport.plist
launchctl bootout system "$P" 2>/dev/null || true
rm -f "$P"
launchctl unsetenv BT_HCI_TRANSPORT
echo "removed"
