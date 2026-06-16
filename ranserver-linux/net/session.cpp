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
    int res = m_sendBuffer.addMsg(framed->data(), framed->size());
    switch (res) {
        case SendMsgBuffer::BUFFER_ERROR:
            Close();
            break;
        case SendMsgBuffer::BUFFER_ADDED:
            break;
        case SendMsgBuffer::BUFFER_SEND: {
            int dwSendSize = m_sendBuffer.getSendSize();
            if (dwSendSize > 0) {
                auto compressed = std::make_shared<std::vector<char>>(
                    m_sendBuffer.getSendBuffer(),
                    m_sendBuffer.getSendBuffer() + dwSendSize
                );
                enqueueWrite(compressed);
            }
            break;
        }
        case SendMsgBuffer::BUFFER_SEND_ADD: {
            int dwSendSize = m_sendBuffer.getSendSize();
            if (dwSendSize > 0) {
                auto compressed = std::make_shared<std::vector<char>>(
                    m_sendBuffer.getSendBuffer(),
                    m_sendBuffer.getSendBuffer() + dwSendSize
                );
                enqueueWrite(compressed);
            }
            m_sendBuffer.addMsg(framed->data(), framed->size());
            break;
        }
    }
}

void Session::Flush() {
    int dwSendSize = m_sendBuffer.getSendSize();
    if (dwSendSize > 0) {
        auto compressed = std::make_shared<std::vector<char>>(
            m_sendBuffer.getSendBuffer(),
            m_sendBuffer.getSendBuffer() + dwSendSize
        );
        enqueueWrite(compressed);
    }
}

void Session::enqueueWrite(std::shared_ptr<const std::vector<char>> packet) {
    bool startWrite = false;
    {
        std::lock_guard<std::mutex> lock(m_writeMx);
        m_writeQueue.push_back(packet);
        if (!m_writeInProgress) {
            m_writeInProgress = true;
            startWrite = true;
        }
    }
    if (startWrite) {
        doWrite();
    }
}

void Session::doWrite() {
    std::shared_ptr<const std::vector<char>> packet;
    {
        std::lock_guard<std::mutex> lock(m_writeMx);
        if (m_writeQueue.empty()) {
            m_writeInProgress = false;
            return;
        }
        packet = m_writeQueue.front();
    }

    auto self = shared_from_this();
    asio::async_write(
        m_sock, asio::buffer(*packet),
        [this, self, packet](const boost::system::error_code& ec, std::size_t /*n*/) {
            if (ec) {
                Close();
                return;
            }
            {
                std::lock_guard<std::mutex> lock(m_writeMx);
                m_writeQueue.pop_front();
            }
            doWrite();
        });
}

void Session::Close() {
    if (m_closed) return;
    m_closed = true;
    boost::system::error_code ec;
    m_sock.shutdown(tcp::socket::shutdown_both, ec);
    m_sock.close(ec);
    
    {
        std::lock_guard<std::mutex> lock(m_writeMx);
        m_writeQueue.clear();
    }
    
    if (m_onClose) m_onClose(m_clientId);
}

std::string Session::RemoteIp() const {
    boost::system::error_code ec;
    auto ep = const_cast<tcp::socket&>(m_sock).remote_endpoint(ec);
    if (ec) return "127.0.0.1";
    return ep.address().to_string();
}

}} // namespace sc::net
