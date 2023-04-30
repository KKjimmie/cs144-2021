#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

using namespace std;

ByteStream::ByteStream(const size_t capacity)
    : _capacity(capacity), _queue(), _written_size(0), _read_size(0), _input_ended(false), _error(false) {}

size_t ByteStream::write(const string &data) {
    if (_input_ended)
        return 0;
    size_t size_to_write = min(data.size(), _capacity - _queue.size());
    for (size_t i = 0; i < size_to_write; i++) {
        _queue.push_back(data[i]);
    }
    _written_size += size_to_write;
    return size_to_write;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    size_t peek_size = min(len, _queue.size());
    return string(_queue.begin(), _queue.begin() + peek_size);
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    // Attention: according to the test case, _read_size is updata here!!!
    size_t pop_size = min(len, _queue.size());
    _queue.erase(_queue.begin(), _queue.begin() + pop_size);
    ;
    _read_size += len;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    string read_data = peek_output(len);
    pop_output(len);
    return read_data;
}

void ByteStream::end_input() { _input_ended = true; }

bool ByteStream::input_ended() const { return _input_ended; }

size_t ByteStream::buffer_size() const { return _queue.size(); }

bool ByteStream::buffer_empty() const { return _queue.empty(); }

bool ByteStream::eof() const { return _queue.empty() && _input_ended; }

size_t ByteStream::bytes_written() const { return _written_size; }

size_t ByteStream::bytes_read() const { return _read_size; }

size_t ByteStream::remaining_capacity() const { return _capacity - _queue.size(); }
