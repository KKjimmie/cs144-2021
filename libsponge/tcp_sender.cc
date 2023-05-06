#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>
#include <iostream>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
//! remote_window_sz 设置为1，这是因为在三次握手时，发送的syn报文可能需要超时重传。
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _timeout(retx_timeout)
    , _timecounter(0)
    , _outstanding_queue()
    , _bytes_in_flight(0)
    , _remote_window_sz(1)
    , _sent_syn(false)
    , _sent_fin(false)
    , _consecutive_retransmissions_count(0) {}

uint64_t TCPSender::bytes_in_flight() const {
    return _bytes_in_flight;
}

void TCPSender::fill_window() {
    // 对方的window size为0时，需要将其看作1
    if (_sent_fin){
        // 如果已经发送过fin了，就不再发送数据了
        return;
    }
    size_t window_sz = _remote_window_sz == 0? 1 : _remote_window_sz;
    if (! _sent_syn){
        // 如果没有发送过 syn,那么第一个发送的数据报文就是syn
        TCPSegment seg;
        _sent_syn = true;
        seg.header().syn = true;
        seg.header().seqno = next_seqno();
        _segments_out.push(seg);
        _outstanding_queue.push(std::make_pair(_next_seqno, seg));
        _next_seqno++;
        _bytes_in_flight += seg.length_in_sequence_space();
    } else if (! _sent_fin && _stream.eof() && window_sz > _bytes_in_flight){
        // 这里需要判断对方窗口大小，判断对方是否还有空间接收fin报文
        // 如果输入结束了，就发送syn报文
        TCPSegment seg;
        _sent_fin = true;
        seg.header().fin = true;
        seg.header().seqno = next_seqno();
        _segments_out.push(seg);
        _outstanding_queue.push(std::make_pair(_next_seqno, seg));
        _next_seqno++;
        _bytes_in_flight += seg.length_in_sequence_space();
    } else {
        // 发送普通的数据报文
        // 发送报文时，需要考虑还没有被ack的报文需要重传，也会占用window size
        while(!_sent_fin && window_sz > _bytes_in_flight && !_stream.buffer_empty()){
            TCPSegment seg;
            size_t len = min(window_sz - _bytes_in_flight, min(TCPConfig::MAX_PAYLOAD_SIZE, _stream.buffer_size()));
            seg.header().seqno = next_seqno();
            seg.payload() = _stream.read(len);
            if (seg.length_in_sequence_space() < window_sz - _bytes_in_flight && _stream.eof()){
                // 如果还有空间且输入结束，则设置fin标志位
                _sent_fin = true;
                seg.header().fin = true;
            }
            _segments_out.push(seg);
            _outstanding_queue.push(std::make_pair(_next_seqno, seg));
            _next_seqno += seg.length_in_sequence_space();
            _bytes_in_flight += seg.length_in_sequence_space();
        }
    }
    // 如果没有正在等待的包，则需要重启计数器
    // if the timer is not running, start it running
    if (_outstanding_queue.empty()){
        _timecounter = 0;
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t abs_ackno = unwrap(ackno, _isn, _next_seqno);
    _remote_window_sz = window_size;
    // 如果接收到的非法的ackno，直接返回
    if (abs_ackno > _next_seqno) return;
    // 否则，查看 outstanding_queue 中还没有被确认的 segment
    bool reset_flag = false;
    while(! _outstanding_queue.empty()){
        auto pair = _outstanding_queue.front();
        // 如果abs_ackno >= abs_seq(pair.first) + length_in_sequence_space()，表明被正确接收
        if(abs_ackno >= pair.first + pair.second.length_in_sequence_space()){
            _bytes_in_flight -= pair.second.length_in_sequence_space();
            _outstanding_queue.pop();
            if (! reset_flag){
                reset_flag = true;
            }
        } else {
            break;
        }
    }
    // 如果有报文被确认了，就可以重置RTO、计数器以及连续重传次数
    if (reset_flag){
        _timeout = _initial_retransmission_timeout;
        _timecounter = 0;
        _consecutive_retransmissions_count = 0;
    }
    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    // 这个函数会被定时调用
    _timecounter += ms_since_last_tick;
    // 当超时了并且有数据需要重传时
    if (_timecounter >= _timeout && !_outstanding_queue.empty()){
        auto pair = _outstanding_queue.front();
        _segments_out.push(pair.second);
        if (_remote_window_sz > 0){
            _timeout *= 2;
        }
        // 记录连续重传次数并且将计数器重置
        _consecutive_retransmissions_count ++;
        _timecounter = 0;
    }
}

unsigned int TCPSender::consecutive_retransmissions() const {
    // 返回连续重传的次数
    return _consecutive_retransmissions_count;
}

void TCPSender::send_empty_segment() {
    // 发送一个空的segment
    TCPSegment seg;
    seg.header().seqno = next_seqno();
    _segments_out.push(seg);
}
