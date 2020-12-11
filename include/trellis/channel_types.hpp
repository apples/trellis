#pragma once

namespace trellis {

template <typename T>
struct channel_type_unreliable_unordered {
    using tag_t = T;
};

template <typename T>
struct channel_type_unreliable_sequenced {
    using tag_t = T;
};

template <typename T>
struct channel_type_reliable_ordered {
    using tag_t = T;
};

template <typename T>
struct channel_type_reliable_unordered {
    using tag_t = T;
};

template <typename T>
struct channel_type_reliable_sequenced {
    using tag_t = T;
};

} // namespace trellis
