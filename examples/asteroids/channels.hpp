#pragma once

#include <trellis/trellis.hpp>

namespace channel {

using sync = trellis::channel_type_reliable_ordered<struct sync_t>;

using state_updates = trellis::channel_type_reliable_sequenced<struct state_updates_t>;

using reliable_messages = trellis::channel_type_reliable_unordered<struct reliable_messages_t>;

template <template <typename...> class T>
using apply_channels = T<sync, state_updates, reliable_messages>;

} // namespace channel
