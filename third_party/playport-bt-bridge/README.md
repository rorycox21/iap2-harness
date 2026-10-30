# PlayPort Bluetooth bridge

Upstream: https://github.com/youcci/playport

Base checkout: `9a0882dd0ffe48e467b59d58b12d81391df55ade` (locally modified). The helper is distributed
under the accompanying upstream GNU GPL version 3 licence. It runs as a
separate process and exchanges raw iAP2 bytes over stdin/stdout.

Local changes include waiting for RFCOMM-open completion before pumping stdin,
direct-address diagnostic pairing, explicit authenticated base connection,
retaining the selected device, deferred service opening, reentrancy guards,
and graceful channel shutdown on EOF/signals. CLI pairing remains experimental;
Apple's native pairing controller is the demonstrated working pairing path.
