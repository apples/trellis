Tutorial
########

Installation
************

Trellis is a header-only library.

The only thing you need to do to include it in your project is Add the `include` directory to your project's compile include directories.

Trellis requires C++17.

For example, using CMake:

.. code-block:: cmake
    
    add_executable(my_game main.cpp)
    target_compile_features(my_game PRIVATE cxx_std_17)
    target_include_directories(my_game PRIVATE
        "${CMAKE_SOURCE_DIR}/ext/trellis/include")

Then, just include the main header in your source code:

.. code-block:: cpp

    #include <trellis/trellis.hpp>

Defining Channels
*****************

Trellis operates on a channel-based protocol.

The recommended way to define a channel type is by using a using-declaration:

.. code-block:: cpp

    using player_updates = trellis::channel_type_reliable_sequenced<struct player_updates_t>;
    using chat_messages = trellis::channel_type_reliable_ordered<struct chat_messages_t>;

The type passed to the channel template does not need to exist, it is only used to make channels distinct.

There are five types of channels:

* ``channel_type_unreliable_unordered``
* ``channel_type_unreliable_sequenced``
* ``channel_type_reliable_ordered``
* ``channel_type_reliable_unordered``
* ``channel_type_reliable_sequenced``

Unreliable vs Reliable
======================

Unreliable channels make no guarantee of message delivery.
Messages may be lost or dropped.

Reliable channels guarantee delivery in some way.
For ordered and unordered channels, this means every message is guaranteed to be delivered.
For sequenced channels, this means that the latest message is guaranteed to be delivered, but earlier messages will be dropped.

Unordered vs Ordered vs Sequenced
=================================

Unordered channels make no effort to sort messages. Each message is processed as soon as it arrives.

Ordered channels guarantee that messages will be processed consecutively in the same order they were sent.
Only reliable channels can make this guarantee.

Sequenced channels guarantee that messages will be processed in the same order they were sent, but not consecutively.
This allows for gaps or "holes" in the message sequence.

Creating Contexts
*****************

Contexts are what encapsulate the underlying socket and executor, and represent either a client or server state.

Contexts must be bound with channels, and the clients and server must have the exact same channel list and channel order.

You must provide an execution context for the Trellis context to run on.
Currently, only ``asio::io_context`` is supported.

.. code-block:: cpp

    auto io = asio::io_context();

    auto work_guard = asio::make_work_guard(*io);

    auto io_thread = std::thread([&]{
        io.run();
    });

In this example, we use a work guard because the ``io_thread`` won't have any work to do until a socket is opened.

.. note::

    Care must be taken to avoid running blocking tasks on all threads.
    Operations which block, such as rendering with v-sync enabled, should be run on their own thread.
    Otherwise, network congestion may occur.

Server Context
==============

.. code-block:: cpp

    using server_context = trellis::server_context<
        player_updates,
        chat_messages>;

    auto server = server_context(io);

    server.listen({asio::ip::udp::v6(), port_num});

The ``.listen()`` method will open the socket to listen for incoming connections on the given ``port_num``.
The parameter is actually an ``asio::ip::udp::endpoint``, and you may construct it however you like.

An IPv6 server context will make an attempt to allow IPv4 connections as well, assuming the network supports IPv4 mapped addresses.

.. note::

    Although ``.listen()`` will open the socket immediately, it cannot actually perform any work until ``io`` is running.
    Make sure to start running ``io`` either before or immediately after calling ``.listen()``.

Client Context
==============

.. code-block:: cpp

    using client_context = trellis::client_context<
        player_updates,
        chat_messages>;

    auto client = client_context(io);

    client.connect(
        {asio::ip::udp::v6(), 0},
        {asio::ip::make_address_v6(server_ip), server_port});

The ``.connect()`` method will open a socket to the server and attempt to form a connection.

The first parameter is the client's local endpoint, and the second parameter is the remote server's endpoint.

It is highly recommended that you use ``0`` as the port number for the client's local endpoint.
This will result in an arbitrary available port being automatically assigned to the connection.
The two endpoints do not need to have matching port numbers.

.. note::

    Although ``.connect()`` will open the socket immediately, it cannot actually perform any work until ``io`` is running.
    Make sure to start running ``io`` either before or immediately after calling ``.connect()``.

Handling Events
***************

In order to process networking events, you must routinely poll the context.

You'll need to create a ``Handler`` object, which will receive the events in the form of method calls.

.. code-block:: cpp

    struct server_handler {
        void on_connect(const server_context::connection_ptr& conn) {
            std::cout
                << "New connection from " << conn->get_endpoint()
                << std::endl;
        }
        
        void on_disconnect(const server_context::connection_ptr& conn, asio::error_code ec) {
            std::cout
                << "Disconnect from " << conn->get_endpoint() << ": "
                << ec.category().name() << ": " << ec.message()
                << std::endl;
        }
        
        void on_receive(player_updates, const server_context::connection_ptr& conn, std::istream& istream) {
            std::cout
                << "Player update message from " << conn->get_endpoint() << ": "
                << istream.rdbuf()
                << std::endl;
        }
        
        void on_receive(chat_messages, const server_context::connection_ptr& conn, std::istream& istream) {
            std::cout
                << "Chat message from " << conn->get_endpoint() << ": "
                << istream.rdbuf()
                << std::endl;
        }
    };

    server.poll_events(server_handler{});

Notice that the ``on_receive`` method is overloaded by the channel types.

This example is a bit contrived.
In a real world scenario, these methods would typically be on your game engine class or something similar, and you would call ``.poll_events(*this)``.

Event handler methods are the exact same for both client and server contexts.

Managing Connections
********************

First, you need to make sure to keep track of the active connections via the handler's ``on_connect`` and ``on_disconnect`` methods.

The ``connection_ptr`` type within each context is a ``std::shared_ptr`` that references the connection object.

Using ``connection_ptr::weak_type`` might be preferable when you expect to store a reference to the connection beyond when the connection is closed.

Getting the Remote Endpoint
===========================

.. code-block:: cpp

    conn->get_endpoint()

The endpoint type is suitable for using as a key in a ``std::map``, but it is not hashable.

This is a good value to use for identifying connections.

Disconnecting
=============

.. code-block:: cpp

    conn->disconenct()

This will make an attempt to gracefully close the connection. It is, however, not guaranteed to be graceful.

If you want to ensure that the remote context knows it's getting disconnected, you'll need to send your own reliable message.

Sending Data
============

.. code-block:: cpp

    conn->send_data<chat_messages>([](std::ostream& ostream) {
        ostream << "Howdy!\n";
    });

The ``.send_data()`` method requires a template parameter which indicates the channel to send the message on.

The callback function is called immediately and synchronously, and the data written to the ``ostream`` will be what is sent in the packet.

.. note::

    You most likely want to use a serialization library such as Cereal or Protobuf instead of writing bytes directly to the stream.
    While writing directly to the stream is possible, it's just not very useful outside of examples.

Shutting Down
*************

.. code-block:: cpp

    server.stop();

All contexts have a ``.stop()`` method which disconnects all active connections and closes the socket.

The context will immediately be considered no longer running.
However, this method is asynchronous, so concurrent tasks may continue to execute before the shutdown is complete.

Don't forget to release any work guards that might be keeping the ``io_context`` running and join the threads.
