# network_utils
Simple network C utilities ( Refer to busybox )

## Functions

- All available ethernet interfaces
- IPv4 address
- IPv6 address, prefix, scope
- IPv6 default gateway
- Mask
- IPv4 Gateway
- MAC address
- Auto-negotiation
- MTU
- Speed (in MB/s)
- Duplex
- Hostname

## Usage
```
  $ gcc network.c -o network
  $ ./network <interface_name>
```
