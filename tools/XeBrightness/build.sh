#!/bin/zsh
# Builds XeBrightness.app (menu bar brightness slider) with the Command Line Tools only, and optionally installs it for the current user.
#   zsh tools/XeBrightness/build.sh              -> build/XeBrightness.app
#   zsh tools/XeBrightness/build.sh install      -> also copies it to ~/Applications and starts it at every login (LaunchAgent)
#   zsh tools/XeBrightness/build.sh uninstall    -> removes the LaunchAgent and the app
set -e
HERE=${0:A:h}; ROOT=${HERE:h:h}
APP=$ROOT/build/XeBrightness.app
DEST=$HOME/Applications/XeBrightness.app
PLIST=$HOME/Library/LaunchAgents/local.xe.brightness.plist
LABEL=local.xe.brightness

if [[ $1 == uninstall ]]; then
  launchctl bootout gui/$(id -u)/$LABEL 2>/dev/null || true
  rm -f $PLIST; rm -rf $DEST
  echo "removed"; exit 0
fi

rm -rf $APP; mkdir -p $APP/Contents/MacOS
clang -fobjc-arc -O2 -framework Cocoa -o $APP/Contents/MacOS/XeBrightness $HERE/main.m
cat > $APP/Contents/Info.plist <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleIdentifier</key><string>local.xe.brightness</string>
  <key>CFBundleName</key><string>XeBrightness</string>
  <key>CFBundleExecutable</key><string>XeBrightness</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSUIElement</key><true/>
</dict></plist>
EOF
codesign --force --sign - $APP >/dev/null 2>&1 || true
echo "built $APP"

if [[ $1 == install ]]; then
  launchctl bootout gui/$(id -u)/$LABEL 2>/dev/null || true
  mkdir -p $HOME/Applications $HOME/Library/LaunchAgents
  rm -rf $DEST; cp -R $APP $DEST
  cat > $PLIST <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key><array><string>$DEST/Contents/MacOS/XeBrightness</string></array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><dict><key>SuccessfulExit</key><false/></dict>
</dict></plist>
EOF
  launchctl bootstrap gui/$(id -u) $PLIST
  echo "installed to $DEST and started (LaunchAgent $LABEL)"
fi
