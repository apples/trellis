#pragma once

#include "datagram.hpp"

#include <iostream>
#include <streambuf>

namespace trellis::_detail {

class bytebuf final : public std::streambuf {
public:
    bytebuf(char* b, char* e) {
        assert(b <= e);
        setg(b, b, e);
        setp(b, e);
    }
};

class ibytestream final : public std::istream {
public:
    ibytestream(const char* b, std::size_t s) : std::istream(&buf), buf(const_cast<char*>(b), const_cast<char*>(b + s)) {}

private:
    bytebuf buf;
};

class obytestream final : public std::ostream {
public:
    obytestream(char* b, char* e) : std::ostream(&buf), buf(b, e) {}

private:
    bytebuf buf;
};

class packetbuf_base : public std::streambuf {
public:
    using std::streambuf::traits_type;
    using std::streambuf::int_type;
    using std::streambuf::pos_type;
    using std::streambuf::off_type;

    using fragment_array = std::array<shared_datagram_buffer, config::max_fragments>;
    using fragment_iterator = fragment_array::iterator;

    static constexpr std::size_t payload_size = config::datagram_size - headers::data_offset;

    packetbuf_base(connection_base& conn) :
        conn(&conn),
        fragments(),
        fragments_back(fragments.begin()),
        current_fragment(fragments.begin()),
        max_pos(0) {}

    packetbuf_base(const packetbuf_base&) = delete;
    packetbuf_base(packetbuf_base&&) = delete;
    packetbuf_base& operator=(const packetbuf_base&) = delete;
    packetbuf_base& operator=(packetbuf_base&&) = delete;

    virtual int_type overflow(int_type ch) {
        if (!traits_type::eq_int_type(ch, traits_type::eof())) {
            if (current_fragment == fragments_back || ++current_fragment == fragments_back) {
                if (current_fragment == fragments.end()) {
                    return traits_type::eof();
                }

                assert(fragments_back < fragments.end());

                *current_fragment = conn->get_context().make_pending_buffer();
                ++fragments_back;
            }

            auto& fragment = *current_fragment;
            auto b = fragment.data() + headers::data_offset;
            auto e = fragment.data() + fragment.size();

            *b = ch;

            setp(b, e);
            pbump(1);

            return ch;
        } else {
            return traits_type::eof();
        }
    }

    virtual pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode which) {
        switch (dir) {
            case std::ios_base::cur: {
                auto cur_pos = pptr() - pbase() + payload_size * (current_fragment - fragments.begin());

                if (off == 0) {
                    return cur_pos;
                }

                auto new_pos = cur_pos + off;

                return seekpos(new_pos, which);
            }
            case std::ios_base::beg: {
                return seekpos(off, which);
            }
            case std::ios_base::end: {
                return seekpos(payload_size * fragments.size() + off, which);
            }
            default:
                return pos_type(off_type(-1));
        }
    }

    virtual pos_type seekpos(pos_type off, std::ios_base::openmode which) {
        assert(which == std::ios_base::out);

        auto cur_pos = pptr() - pbase() + payload_size * (current_fragment - fragments.begin());

        max_pos = std::max(off_type(cur_pos), off_type(max_pos));

        if (which != std::ios_base::out) {
            return pos_type(off_type(-1));
        }

        auto new_pos = off;

        if (new_pos > pos_type(payload_size * fragments.size())) {
            return pos_type(off_type(-1));
        }

        auto new_index = new_pos / payload_size;
        auto new_offset = new_pos % payload_size;

        if (new_index == fragments.size()) {
            assert(new_offset == 0);
            new_index = fragments.size() - 1;
            new_offset = payload_size;
        }

        assert(new_index < fragments.size());
        assert(new_offset <= payload_size);

        current_fragment = fragments.begin() + new_index;

        assert(current_fragment < fragments.end());

        for (; fragments_back <= current_fragment; ++fragments_back) {
            *fragments_back = conn->get_context().make_pending_buffer();
            fragments_back->clear();
        }

        assert(current_fragment < fragments_back);

        auto& fragment = *current_fragment;
        auto b = fragment.data() + headers::data_offset;
        auto e = fragment.data() + fragment.size();

        setp(b, e);
        pbump(new_offset);

        return new_pos;
    }

protected:
    connection_base* conn;
    fragment_array fragments;
    fragment_iterator fragments_back;
    fragment_iterator current_fragment;
    pos_type max_pos;
};

template <typename C>
class packetbuf final : public packetbuf_base {
public:
    using connection_type = C;

    packetbuf(connection_type& conn) : packetbuf_base(conn) {}

    packetbuf(const packetbuf&) = delete;
    packetbuf(packetbuf&&) = delete;
    packetbuf& operator=(const packetbuf&) = delete;
    packetbuf& operator=(packetbuf&&) = delete;

    template <typename Channel>
    void send() {
        auto total_size = std::max(off_type(max_pos), off_type(seekoff(0, std::ios_base::cur, std::ios_base::out)));
        assert(total_size <= off_type((fragments_back - fragments.begin()) * payload_size));

        auto last_payload_size = total_size % payload_size;
        assert(last_payload_size <= payload_size);

        static_cast<connection_type*>(conn)->template send_data<Channel>(fragments.begin(), fragments_back, last_payload_size);
    }
};

template <typename Channel, typename C>
class opacketstream final : public std::ostream {
public:
    using channel_type = Channel;
    using connection_type = C;
    using buf_type = packetbuf<connection_type>;

    opacketstream(connection_type& conn) : std::ostream(&buf), buf(conn) {}

    ~opacketstream() {
        buf.template send<channel_type>();
    }

private:
    buf_type buf;
};

} // namespace trellis::_detail
