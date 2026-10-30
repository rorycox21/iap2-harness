#!/usr/bin/env python3
"""Check the selected bond, invoke native pairing if needed, then run one receiver."""
import argparse
import getpass
import re
import signal
import os
from mac_hotspot import hotspot_config
from pathlib import Path
import subprocess
import sys


def normalized(address):
    return address.replace('-', ':').upper()


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', required=True)
    parser.add_argument('--mfi-cert', type=Path)
    parser.add_argument('--mfi-key', type=Path)
    parser.add_argument('--hotspot', action='store_true', help='Use the active Mac Internet Sharing Wi-Fi network and address')
    parser.add_argument('--cli-pair', action='store_true', help='Try experimental direct pairing before the native controller')
    parser.add_argument('--bt-bridge', type=Path, default=root / 'build/bt-bridge')
    parser.add_argument('--binary', type=Path, default=root / 'build/iap2-harness')
    parser.add_argument('--pairing-app', type=Path, default=root / 'build/iAP2-Pairing.app')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--capture', action='store_true')
    mode.add_argument('--receiver', action='store_true')
    args, extra = parser.parse_known_args()
    if not re.fullmatch(r'(?:[0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}', args.address):
        parser.error('--address must be the selected iPhone Bluetooth address')
    if not args.capture:
        for name, path in (('--mfi-cert', args.mfi_cert), ('--mfi-key', args.mfi_key)):
            if path is None or not path.expanduser().is_file():
                parser.error(f'{name} must point to your existing credential file outside the repository')
            extra.extend([name, str(path.expanduser().resolve())])
    env = os.environ.copy()
    if args.hotspot:
        network = hotspot_config()
        if not args.capture and not env.get('CARPLAY_WIFI_PASSPHRASE'):
            if not sys.stdin.isatty():
                parser.error('Run in a terminal for the hidden hotspot password prompt, or set CARPLAY_WIFI_PASSPHRASE')
            env['CARPLAY_WIFI_PASSPHRASE'] = getpass.getpass('Existing hotspot Wi-Fi password (hidden): ')
        if not args.capture and not 8 <= len(env['CARPLAY_WIFI_PASSPHRASE'].encode('utf-8')) <= 63:
            parser.error('The hotspot password must contain 8–63 UTF-8 bytes')
        if any(option in extra for option in ('--ssid', '--wifi-ip', '--wifi-channel', '--wifi-security')):
            parser.error('--hotspot supplies SSID, IP, channel and security; omit explicit Wi-Fi options')
        extra.extend(['--ssid', network['ssid'], '--wifi-ip', network['wifi_ip'],
                      '--wifi-channel', str(network['channel']), '--wifi-security', str(network['security_type'])])
        print(f"Using Mac hotspot {network['ssid']} at {network['wifi_ip']} on channel {network['channel']}", flush=True)
    bridge = args.bt_bridge.expanduser().resolve()
    binary = args.binary.expanduser().resolve()
    app = args.pairing_app.expanduser().resolve()
    for executable in (bridge, binary):
        if not executable.is_file():
            parser.error(f'Executable missing: {executable}')

    def paired():
        result = subprocess.run([str(bridge), '--list'], capture_output=True, text=True, timeout=8)
        if result.returncode:
            raise RuntimeError(result.stderr.strip() or 'Cannot query Bluetooth pairing')
        return any(normalized(line.split()[0]) == normalized(args.address)
                   for line in result.stdout.splitlines() if line.split())

    if args.cli_pair and not paired():
        print('Starting programmatic Bluetooth pairing; accept matching codes on the iPhone.', flush=True)
        try:
            subprocess.run([str(bridge), '--force-pair', args.address], timeout=60)
        except subprocess.TimeoutExpired:
            print('Command-line pairing timed out; trying Apple’s native dialog.', flush=True)
    if not paired():
        executable = app / 'Contents/MacOS/iAP2-Pairing'
        source = root / 'scripts/mac-pairing/main.m'
        if not executable.exists() or source.stat().st_mtime > executable.stat().st_mtime:
            subprocess.run([str(root / 'scripts/mac-pairing/build.sh'), str(app)], check=True)
        print('Keep iPhone Settings → Bluetooth open. Select your iPhone in Apple’s dialog and accept matching codes.', flush=True)
        subprocess.run(['/usr/bin/open', '-n', '-W', str(app), '--args', '--address', args.address, '--once'], check=True)
        if not paired():
            raise RuntimeError('Native pairing did not save the selected iPhone bond; receiver was not started')
    print('Mac has a saved pairing; starting a single full-duplex bridge. Connection will verify the bond.', flush=True)
    if '--bt-mac' not in extra:
        local = subprocess.run([str(bridge), '--print-address'], capture_output=True, text=True, check=True, timeout=8).stdout.strip()
        if not re.fullmatch(r'(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}', local):
            raise RuntimeError('Could not determine this Mac’s Bluetooth address')
        extra.extend(['--bt-mac', local])
    command = [str(binary), '--capture' if args.capture else '--receiver',
               '--address', args.address, '--bt-bridge', str(bridge), *extra]
    receiver = subprocess.Popen(command, cwd=root, env=env, start_new_session=True)
    try:
        return receiver.wait()
    except KeyboardInterrupt:
        os.killpg(receiver.pid, signal.SIGTERM)
        receiver.wait()
        return 130
    finally:
        # Clean up this receiver's bridge and Bonjour children on every exit.
        try:
            os.killpg(receiver.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (RuntimeError, subprocess.SubprocessError, OSError) as error:
        print(f'carplay: {error}', file=sys.stderr)
        sys.exit(1)
