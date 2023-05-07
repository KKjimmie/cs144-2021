#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    // 如果接收到含有rst标记位的报文，则断开连接
    _time_since_last_segment_received = 0;
    // keep-alive
    if (_receiver.ackno().has_value() && seg.length_in_sequence_space() == 0 && seg.header().seqno == _receiver.ackno().value() - 1) {
        _sender.send_empty_segment();
        send_segments();
        return;
    }
    _receiver.segment_received(seg);

    if (seg.header().rst) {
        _receiver.stream_out().set_error();
        _sender.stream_in().set_error();
        _linger_after_streams_finish = false;
        _is_alive = false;
        return;
    }

    // 如果收到的报文设置了ack标志位，则将ackno和windowsize传输给_sender
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
        send_segments();
    }

    // Listen -> syn-recvd
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::SYN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::CLOSED) {
        connect();
        return;
    }

    // 判断 TCP 断开连接时是否时需要等待
    // CLOSE_WAIT
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_ACKED)
        _linger_after_streams_finish = false;

    // established -> close_wait
    // 服务器先断开
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED && !_linger_after_streams_finish) {
        _is_alive = false;
        return;
    }

    // 发送ack报文
    if (seg.length_in_sequence_space() != 0 && _sender.segments_out().empty()) {
        _sender.send_empty_segment();
        send_segments();
    }
}

bool TCPConnection::active() const { return _is_alive; }

size_t TCPConnection::write(const string &data) {
    size_t w_size = _sender.stream_in().write(data);
    // 写入数据到bytestream后，既可以发送了
    _sender.fill_window();
    send_segments();
    return w_size;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _sender.tick(ms_since_last_tick);

    // 查看重传次数，如果超时，发送rst报文
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        send_rst_seg();
        _receiver.stream_out().set_error();
        _sender.stream_in().set_error();
        _linger_after_streams_finish = false;
        _is_alive = false;
        return;
    }
    //发送报文，因为sender在调用tick函数之后有可能重传报文
    send_segments();
    _time_since_last_segment_received += ms_since_last_tick;

    // 在tcp连接进入time_wait状态时，当等待的时间超过一定上限，则可以 clean close
    // time wait 状态是主动发起fin报文才回出现的状态
    // 发送fin报文后，状态由establish -> fin_wait1
    // 收到ack：fin_wait1 -> fin_wait2
    // 收到对方的fin报文并发送ack： fin_wait2 -> time_wait
    // 过了 2MSL ：fin_wait2 -> closed
    // 各种状态在sender以及receiver上的表现参考tcp_state.hh 和 tcp_state.cc
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED && _linger_after_streams_finish &&
        _time_since_last_segment_received >= 10 * _cfg.rt_timeout) {
        _is_alive = false;
        _linger_after_streams_finish = false;
    }
}

void TCPConnection::end_input_stream() {
    // 结束输入
    _sender.stream_in().end_input();
    // 发送 fin
    _sender.fill_window();
    // 将segment发送出去
    send_segments();
}

void TCPConnection::connect() {
    // 连接，发送syn报文
    // sender 在一开始时会发送一个SYN报文
    _is_alive = true;
    _sender.fill_window();
    send_segments();
}

void TCPConnection::send_rst_seg() {
    // 先清空_sender的发送缓冲区
    while (!_sender.segments_out().empty()) {
        _sender.segments_out().pop();
    }
    // 发送一个空的数据报文
    _sender.send_empty_segment();
    TCPSegment rst_seg = _sender.segments_out().front();
    _sender.segments_out().pop();
    rst_seg.header().rst = true;
    _segments_out.push(rst_seg);
}

void TCPConnection::send_segments() {
    while (!_sender.segments_out().empty()) {
        auto seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        if (_receiver.ackno().has_value()) {
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
        }
        seg.header().win = _receiver.window_size();
        _segments_out.push(seg);
    }
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            // Your code here: need to send a RST segment to the peer
            send_rst_seg();
            _receiver.stream_out().set_error();
            _sender.stream_in().set_error();
            _linger_after_streams_finish = false;
            _is_alive = false;
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
