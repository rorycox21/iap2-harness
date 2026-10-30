# Optional Raspberry Pi Ethernet upstream

Use this when the Pi has Wi-Fi internet and its Ethernet cable connects directly
to the Mac. On Raspberry Pi OS with NetworkManager, run over SSH (preferably
through Wi-Fi):

```sh
sudo nmcli connection add type ethernet ifname eth0 \
  con-name Mac-CarPlay-Uplink \
  ipv4.method shared ipv4.addresses 10.42.0.1/24 \
  ipv4.never-default yes ipv6.method disabled \
  connection.autoconnect yes
sudo nmcli connection up Mac-CarPlay-Uplink
```

If that profile already exists, activate it instead of adding another. If your
Ethernet interface has another name, replace `eth0`. Ethernet SSH can disconnect
when the interface changes address. This uses NetworkManager shared mode to
provide DHCP/NAT on Ethernet while Wi-Fi remains upstream.

On the Mac, enable the USB Ethernet service, select DHCP and verify an address
in `10.42.0.0/24` with router `10.42.0.1`. Then share that Ethernet connection
to Wi-Fi in Internet Sharing. Do not use the Pi Ethernet address as the CarPlay
endpoint; the phone connects to the Mac hotspot's bridge address instead.

Reference: [NetworkManager IPv4 shared mode](https://networkmanager.dev/docs/api/1.44/nm-settings-nmcli.html).
