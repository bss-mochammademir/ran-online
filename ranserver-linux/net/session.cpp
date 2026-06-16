#include "session.h"

#include <utility>
#include <vector>

namespace sc { namespace net {

namespace asio = boost::asio;
using tcp = boost::asio::ip::tcp;

Session::Session(tcp::socket sock, uint32_t clientId,
                 MessageHandler onMsg, CloseHandler onClose)
    : m_sock(std::move(sock)),
      m_clientId(clientId),
      m_onMsg(std::move(onMsg)),
      m_onClose(std::move(onClose)) {}

void Session::Start() { doRead(); }

void Session::doRead() {
    auto self = shared_from_this();
    m_sock.async_read_some(
        asio::buffer(m_readBuf),
        [this, self](const boost::system::error_code& ec, std::size_t n) {
            if (ec) { Close(); return; }

            std::vector<Message> msgs;
            if (!m_framer.Feed(m_readBuf.data(), n, msgs)) {
                // protocol error (bad frame length) -> drop the link
                Close();
                return;
            }
            for (auto& m : msgs) {
                m.clientId = m_clientId;
                if (m_onMsg) m_onMsg(m_clientId, std::move(m));
            }
            doRead();
        });
}

void Session::Send(std::shared_ptr<const std::vector<char>> framed) {
    auto self = shared_from_this();
    asio::async_write(
        m_sock, asio::buffer(*framed),
        [this, self, framed](const boost::system::error_code& ec, std::size_t /*n*/) {
            if (ec) Close();
        });
}

void Session::Close() {
    if (m_closed) return;
    m_closed = true;
    boost::system::error_code ec;
    m_sock.shutdown(tcp::socket::shutdown_both, ec);
    m_sock.close(ec);
    if (m_onClose) m_onClose(m_clientId);
}

}} // namespace sc::net
