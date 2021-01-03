#pragma once

namespace trellis {

struct connection_stats {
    int outgoing_queue_size;
    int num_awaiting;
};

} // namespace trellis
