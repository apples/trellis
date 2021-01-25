#pragma once

#include "channel_types.hpp"
#include "channel_unreliable_unordered.hpp"
#include "channel_unreliable_sequenced.hpp"
#include "channel_reliable_ordered.hpp"
#include "channel_reliable_unordered.hpp"
#include "channel_reliable_sequenced.hpp"

namespace trellis::_detail {

/** Connection-specific channel type. */
template <typename Connection, typename ChannelDef>
class channel;

template <typename Connection, typename Tag>
class channel<Connection, channel_type_unreliable_unordered<Tag>> : public channel_unreliable_unordered {
public:
    using channel_unreliable_unordered::channel_unreliable_unordered;
};

template <typename Connection, typename Tag>
class channel<Connection, channel_type_unreliable_sequenced<Tag>> : public channel_unreliable_sequenced {
public:
    using channel_unreliable_sequenced::channel_unreliable_sequenced;
};

template <typename Connection, typename Tag>
class channel<Connection, channel_type_reliable_ordered<Tag>> : public channel_reliable_ordered {
public:
    using channel_reliable_ordered::channel_reliable_ordered;
};

template <typename Connection, typename Tag>
class channel<Connection, channel_type_reliable_unordered<Tag>> : public channel_reliable_unordered {
public:
    using channel_reliable_unordered::channel_reliable_unordered;
};

template <typename Connection, typename Tag>
class channel<Connection, channel_type_reliable_sequenced<Tag>> : public channel_reliable_sequenced {
public:
    using channel_reliable_sequenced::channel_reliable_sequenced;
};

} // namespace trellis::_detail
