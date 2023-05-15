#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address)
    , _ip_address(ip_address)
    , _ms_time_passed(0)
    , _ip_eth_table()
    , _sent_arp()
    , _dgram_buf() {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();

    auto it = _ip_eth_table.find(next_hop_ip);
    // 如果缓存中有ip->eth
    if (it != _ip_eth_table.end()) {
        EthernetFrame eth_frame;
        eth_frame.header().src = _ethernet_address;
        eth_frame.header().dst = it->second.first;
        eth_frame.header().type = EthernetHeader::TYPE_IPv4;
        eth_frame.payload() = dgram.serialize();
        _frames_out.push(eth_frame);
        return;
    }

    // 如果没有，则发送arp请求
    if (_sent_arp.find(next_hop_ip) == _sent_arp.end()) {
        ARPMessage arp_request;
        arp_request.opcode = ARPMessage::OPCODE_REQUEST;
        arp_request.sender_ethernet_address = _ethernet_address;
        arp_request.sender_ip_address = _ip_address.ipv4_numeric();
        arp_request.target_ip_address = next_hop_ip;

        EthernetFrame eth_frame;
        eth_frame.header().src = _ethernet_address;
        eth_frame.header().dst = ETHERNET_BROADCAST;
        eth_frame.header().type = EthernetHeader::TYPE_ARP;
        eth_frame.payload() = arp_request.serialize();
        _frames_out.push(eth_frame);

        _sent_arp[next_hop_ip] = _ms_time_passed;

        if (_dgram_buf.find(next_hop_ip) != _dgram_buf.end()) {
            _dgram_buf[next_hop_ip] = std::queue<InternetDatagram>();
        }
        _dgram_buf[next_hop_ip].push(dgram);
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    if (frame.header().dst != _ethernet_address && frame.header().dst != ETHERNET_BROADCAST)
        return {};

    // 收到 ipv4 数据包
    if (frame.header().type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram dgram;
        if (dgram.parse(frame.payload()) != ParseResult::NoError)
            return {};
        return dgram;
    }

    // 收到 arp
    if (frame.header().type == EthernetHeader::TYPE_ARP) {
        ARPMessage arp_msg;
        if (arp_msg.parse(frame.payload()) != ParseResult::NoError)
            return {};

        _ip_eth_table[arp_msg.sender_ip_address] = std::make_pair(arp_msg.sender_ethernet_address, _ms_time_passed);
        if (_dgram_buf.find(arp_msg.sender_ip_address) != _dgram_buf.end()) {
            while (!_dgram_buf[arp_msg.sender_ip_address].empty()) {
                auto dgram = _dgram_buf[arp_msg.sender_ip_address].front();
                _dgram_buf[arp_msg.sender_ip_address].pop();
                Address next_hop = Address::from_ipv4_numeric(arp_msg.sender_ip_address);
                send_datagram(dgram, next_hop);
            }
            _dgram_buf.erase(arp_msg.sender_ip_address);
        }

        if (arp_msg.opcode == ARPMessage::OPCODE_REQUEST && arp_msg.target_ip_address == _ip_address.ipv4_numeric()) {
            ARPMessage arp_reply;
            arp_reply.opcode = ARPMessage::OPCODE_REPLY;
            arp_reply.sender_ethernet_address = _ethernet_address;
            arp_reply.sender_ip_address = _ip_address.ipv4_numeric();
            arp_reply.target_ethernet_address = arp_msg.sender_ethernet_address;
            arp_reply.target_ip_address = arp_msg.sender_ip_address;

            EthernetFrame eth_frame;
            eth_frame.header().src = _ethernet_address;
            eth_frame.header().dst = arp_msg.sender_ethernet_address;
            eth_frame.header().type = EthernetHeader::TYPE_ARP;
            eth_frame.payload() = arp_reply.serialize();
            _frames_out.push(eth_frame);
        }
    }
    return {};
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    _ms_time_passed += ms_since_last_tick;

    // 1. _ip_eth_table 过期，30s
    if (_ms_time_passed >= 30000) {
        size_t expire_time = _ms_time_passed - 30000;
        for (auto it = _ip_eth_table.begin(); it != _ip_eth_table.end();) {
            if (it->second.second <= expire_time) {
                it = _ip_eth_table.erase(it);
            } else {
                ++it;
            }
        }
    }

    // 2. _sent_arp 过期，5s
    if (_ms_time_passed >= 5000) {
        size_t expire_time = _ms_time_passed - 5000;
        for (auto it = _sent_arp.begin(); it != _sent_arp.end();) {
            if (it->second <= expire_time) {
                it = _sent_arp.erase(it);
            } else {
                ++it;
            }
        }
    }
}
