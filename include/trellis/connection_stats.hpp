#pragma once

namespace trellis {

/** Simple stats about a connection. */
struct connection_stats {
    int outgoing_queue_size; /** How many packets are waiting in the outgoing queue. */
    int num_awaiting; /** How many packets are currently expected to be received. */
};

} // namespace trellis
