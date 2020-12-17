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
- [x] Unreliable, sequenced channels
- [x] Reliable, unordered channels
- [x] Reliable, ordered channels
- [x] Reliable, sequenced channels
- [x] Network condition simulation proxy
- [x] Unit tests

## Dependencies

- [Asio](https://think-async.com/Asio/) - Required for core behavior.
- [Cereal](https://uscilab.github.io/cereal/) - Used for examples.
- [SDL2](https://www.libsdl.org/) - Used for examples.

## Examples Assets Acknowledgements

- [Fonts](https://nimblebeastscollective.itch.io/magosfonts)

## Usage

Trellis is a header-only library.

See the `examples/pingpong/` directory for a simple example.

## License

MIT license. See LICENSE.md.
