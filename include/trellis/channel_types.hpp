#pragma once

namespace trellis {

/** Describes a channel which is unreliable and unordered. */
template <typename T>
struct channel_type_unreliable_unordered {
    using tag_t = T;
};

/** Describes a channel which is unreliable and sequenced. */
template <typename T>
struct channel_type_unreliable_sequenced {
    using tag_t = T;
};

/** Describes a channel which is reliable and ordered. */
template <typename T>
struct channel_type_reliable_ordered {
    using tag_t = T;
};

/** Describes a channel which is reliable and unordered. */
template <typename T>
struct channel_type_reliable_unordered {
    using tag_t = T;
};

/** Describes a channel which is reliable and sequenced. */
template <typename T>
struct channel_type_reliable_sequenced {
    using tag_t = T;
};

} // namespace trellis
