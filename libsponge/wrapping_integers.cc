#include "wrapping_integers.hh"

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    uint32_t value = static_cast<uint32_t>((n + isn.raw_value()) & 0xFFFFFFFF);
    return WrappingInt32{value};
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    uint32_t offset = n.raw_value() - isn.raw_value();
    // 如果 offset >= checkpoint，表明这两个值都在 32bit范围内，此时offset就是n距离checkpoint最近的absolute seqno
    if(offset >= checkpoint){
        return offset;
    }
    uint32_t half = 1ull << 31;
    // 取 checkpoint 的高位
    uint64_t result = offset + (checkpoint & 0xFFFFFFFF00000000);
    // 如何求出离checkpoint最近的？
    // result位置可能在下面这两个位置之一：
    // | 表示中点
    // ||-------|----(1)----checkpoint----(2)---|---------checkpoint+2^32||
    // 如果此时的result比checkpoint+2^31还要大，那就表明n的absulute seqno应该在checkpoint前面的(1)，才满足 "closest"这个要求
    if (result > checkpoint + half) {
        result -= (1ull << 32);
    // 否则，如果result比checkpoint-2^31还要小,则表明result应该在(2)位置
    // 需要注意，checkpoint在此时应该是大于1<<31，否则相减得到的数是负数，转化为 uint64_t 就是非常大的正数。
    // 例如：-----0--------cp-----1<<31--------offset----1<<32-----------------(3)--
    // 此时，offset位置就是result位置，如果不判断checkpoint > half，就会得到offset在位置(3)，显然不正确
    } else if (checkpoint > half && result <= checkpoint - half) {
        result += (1ull << 32);
    }
    return result;
}
