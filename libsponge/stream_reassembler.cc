#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _unassembled_queue(capacity, '\0')
    , _flags_queue(capacity, false)
    , _is_eof(false)
    , _eof_idx(0)
    , _unassembled_size(0)
    , _output(capacity)
    , _capacity(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    // substrings provided to the push substring() function may overlap
    size_t first_unassembled = _output.bytes_written();
    size_t first_unaccept = _output.bytes_read() + _capacity;

    // 处理那些在范围之外内的数据
    // 注意，有可能数据在范围之外，但是其 eof 标志位为1，因此，对于在范围之外的数据不能简单丢弃
    if (!(index >= first_unaccept || index + data.size() <= first_unassembled)) {
        size_t begin_idx = max(index, first_unassembled);
        size_t end_index = min(first_unaccept, index + data.size());

        // 将数据存放到 _unassembled_queue 中
        for (size_t i = begin_idx; i < end_index; i++) {
            if (!_flags_queue[i - first_unassembled]) {
                _unassembled_queue[i - first_unassembled] = data[i - index];
                _unassembled_size++;
                _flags_queue[i - first_unassembled] = true;
            }
        }

        // 将有序的数据写入 ByteStream
        string to_write = "";
        while (_flags_queue.front() && to_write.size() < _output.remaining_capacity()) {
            to_write += _unassembled_queue.front();
            _unassembled_queue.pop_front();
            _flags_queue.pop_front();
            // 入队占位，保持队列容量不变，类似滑动窗口
            _unassembled_queue.emplace_back('\0');
            _flags_queue.emplace_back(false);
        }

        if (to_write.size() > 0) {
            _unassembled_size -= to_write.size();
            _output.write(to_write);
        }
    }

    if (eof) {
        _eof_idx = index + data.size();
        _is_eof = true;
    }
    if (_is_eof && _eof_idx == _output.bytes_written()) {
        _output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembled_size; }

bool StreamReassembler::empty() const { return unassembled_bytes() == 0; }
