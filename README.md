# Trellis

Trellis is a connection-oriented UDP networking framework designed primarily for games.

It is currently experimental, incomplete, and unstable.

## Features

### Connections

Trellis uses a connection-oriented protocol, for easy management of connected clients.

Each client is given a single connection, which can have multiple separate channels.

### Channels

All connections share a structure of channels, and must have at least one.

Messages must be sent on a specific channel.

Each channel can be configured differently, being either reliable or unreliable, and ordered or unordered.

### Fragmentation

Large messages are automatically fragmented and reassembled according to the MTU size.

Reliable channels, in the event of packet loss, only resend the fragments that were lost.

## TODO

- [x] Unreliable, unordered channels
- [ ] Reliable, unordered channels
- [ ] Unreliable, ordered channels
- [ ] Reliable, ordered channels
- [ ] Network condition simulation proxy
- [ ] Unit tests

## Usage

Trellis is a header-only library.

See the `examples/pingpong/` directory for a simple example.

## License

MIT license. See LICENSE.md.
