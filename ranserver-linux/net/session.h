#ifndef SC_NET_SESSION_H
#define SC_NET_SESSION_H

#include <array>
#include <functional>
#include <memory>
#include <boost/asio.hpp>
#include "packet.h"

namespace sc { namespace net {

// Session — one connected client. Runs the async read loop (the cross-platform
// replacement for the IOCP recv-completion path + PER_IO_OPERATION_DATA buffer):
// async_read_some -> MessageFramer -> invoke the message handler per complete
// NET_MSG_GENERIC frame. Held by shared_ptr so it stays alive across async ops.
class Session : public std::enable_shared_from_this<Session> {
public:
    // onMsg is invoked (on an io_context thread) for each complete inbound message;
    // onClose is invoked once when the link drops (read error / protocol error / EOF).
    using MessageHandler = std::function<void(uint32_t clientId, Message&&)>;
    using CloseHandler   = std::function<void(uint32_t clientId)>;

    Session(boost::asio::ip::tcp::socket sock, uint32_t clientId,
            MessageHandler onMsg, CloseHandler onClose);

    void Start();                                   // begin the read loop
    void Send(std::shared_ptr<const std::vector<char>> framed);  // async write a framed message
    void Close();

    uint32_t ClientId() const { return m_clientId; }

private:
    void doRead();

    boost::asio::ip::tcp::socket m_sock;
    uint32_t                     m_clientId;
    MessageHandler               m_onMsg;
    CloseHandler                 m_onClose;
    MessageFramer                m_framer;
    std::array<char, kMaxMessageSize> m_readBuf;
    bool                         m_closed = false;
};

}} // namespace sc::net

#endif // SC_NET_SESSION_H
