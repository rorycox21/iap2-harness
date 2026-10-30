"""Read the active macOS Internet Sharing Wi-Fi endpoint without reading passwords."""
import json
import plistlib
import re
import subprocess
from pathlib import Path


def hotspot_config():
    preferences = plistlib.loads(Path('/Library/Preferences/SystemConfiguration/com.apple.airport.preferences.plist').read_bytes())
    configured = preferences.get('InternetSharing', {})
    ssid = configured.get('SSID', b'')
    if isinstance(ssid, bytes):
        ssid = ssid.decode('utf-8')
    if not ssid:
        raise RuntimeError('Configure the hotspot name in System Settings → General → Sharing → Internet Sharing')
    result = subprocess.run(['/sbin/ifconfig', '-a'], capture_output=True, text=True, check=True)
    interfaces = {}
    for block in re.split(r'(?=^[^\s:]+: flags=)', result.stdout, flags=re.M):
        if ': flags=' in block:
            interfaces[block.split(':', 1)[0]] = block
    for name, block in interfaces.items():
        if not name.startswith('bridge'):
            continue
        members = re.findall(r'^\s+member: (\S+)', block, re.M)
        ap = next((m for m in members if m.startswith('ap') and
                   re.search(r'^\s+status: active$', interfaces.get(m, ''), re.M)), None)
        address = re.search(r'^\s+inet (\d+\.\d+\.\d+\.\d+) ', block, re.M)
        if ap and address:
            channel = int(configured.get('Channel', 0))
            if not channel:
                raise RuntimeError('Choose a fixed Wi-Fi channel in Internet Sharing before starting CarPlay')
            return {'ssid': ssid, 'wifi_ip': address.group(1), 'channel': channel,
                    'security_type': 2, 'bridge': name, 'access_point': ap}
    raise RuntimeError('The hotspot is configured but no active Wi-Fi access point with an IPv4 bridge was found. Connect Ethernet and enable Internet Sharing to Wi-Fi.')


if __name__ == '__main__':
    try:
        print(json.dumps(hotspot_config(), indent=2))
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'hotspot: {error}')
        raise SystemExit(1)
