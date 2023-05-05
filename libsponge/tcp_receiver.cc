#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    if (! _got_syn && !seg.header().syn){
        return;
    }

    WrappingInt32 seqno = seg.header().seqno;
    if (! _got_syn && seg.header().syn){
        _got_syn = true;
        _isn = seqno;
        // 由于SYN标志位是没有数据但是占用一个序列号，因此需要加一，得到的是数据的seqno
        seqno = seqno + 1;
    }
    // 获取上一次读入的数据的最后一个字节的idx作为checkpoint
    uint64_t checkpoint = _reassembler.stream_out().bytes_written();
    // 对于stream_idx，应该为abs_seq - 1,
    size_t stream_idx = unwrap(seqno, _isn, checkpoint) - 1;
    _reassembler.push_substring(seg.payload().copy(), stream_idx, _got_syn && seg.header().fin);
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    // akono 是接收端希望接收的下一个字节的序号
    if (! _got_syn){
        return {};
    }
    size_t ackno = _reassembler.stream_out().bytes_written() + 1;
    // 如果接收到的报文中出现了FIN，由于FIN占用一个序列号，但是没有实际数据，因此需要加一
    if (stream_out().input_ended()){
        return wrap(ackno+1, _isn);
    }
    return wrap(ackno, _isn);
}

size_t TCPReceiver::window_size() const {
    // window_size 应该是 first unassembled 到 first unaccept 之间的距离
    // size_t first_unassemble = _reassembler.stream_out().bytes_written();
    // size_t first_unaccept = _reassembler.stream_out().bytes_read() + _capacity;
    // return first_unaccept - first_unassemble;
    return _reassembler.stream_out().remaining_capacity();
}
