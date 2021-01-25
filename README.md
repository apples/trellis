# Trellis

Trellis is a connection-oriented UDP networking framework designed primarily for games.

It is easy to use and beginner-friendly. Basic knowledge of networking and threads is assumed.

It is currently only intended for experimental purposes, but the API should be fairly stable.

High performance is not a goal, and neither is handling weird network conditions. This is mostly just for fun.

## Documentation

High-level documentation is available at [https://trellis.readthedocs.io/en/latest/](https://trellis.readthedocs.io/en/latest/).

The public API is documented with Doxygen comments, which can be converted to HTML using the included `Doxyfile`.

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

### Header Only

Being header-only allows Trellis to be included in your project in a familiar and easy way.

No special compile flags or third-party binaries are needed.

## Dependencies

- [Asio](https://think-async.com/Asio/) (submodule) - Required for core behavior.
- [Cereal](https://uscilab.github.io/cereal/) (submodule) - Used for examples.
- [SDL2](https://www.libsdl.org/) - Used for Asteroids example.

## Usage

Trellis is a header-only library.

The `CMakeLists.txt` is responsible for building everything.

See the `examples/pingpong/` directory for a simple example.

See the `examples/asteroids/` directory for a more realistic example.

## License

MIT license. See LICENSE.md.
