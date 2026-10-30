#!/bin/sh
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_path=${1:?Usage: build.sh /absolute/path/iAP2-Pairing.app}
mkdir -p "$app_path/Contents/MacOS"
clang -fobjc-arc -Wall -Wextra -Wno-unused-parameter -Wno-incompatible-pointer-types -o "$app_path/Contents/MacOS/iAP2-Pairing" "$source_dir/main.m" -framework AppKit -framework IOBluetooth -framework IOBluetoothUI
cat > "$app_path/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>iAP2-Pairing</string>
<key>CFBundleIdentifier</key><string>local.iap2-harness.pairing</string>
<key>CFBundleName</key><string>iAP2 Pairing</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleVersion</key><string>1</string>
<key>CFBundleShortVersionString</key><string>0.1</string>
<key>NSBluetoothAlwaysUsageDescription</key><string>Pair your selected iPhone with this Mac to test the local iAP2 receiver.</string>
<key>NSBluetoothPeripheralUsageDescription</key><string>Pair your selected iPhone with this Mac to test the local iAP2 receiver.</string>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
codesign --force --sign - "$app_path"
