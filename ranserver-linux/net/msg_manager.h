#ifndef SC_NET_MSG_MANAGER_H
#define SC_NET_MSG_MANAGER_H

#include <deque>
#include <mutex>
#include "packet.h"

namespace sc { namespace net {

// MsgManager — port of the double-buffering receive queue sc::net::MsgManager
// (SigmaCore/Net/MsgList.{h,cpp}). The original decouples the IOCP worker threads
// (which enqueue received packets) from the game-logic thread (which processes
// them) via two lists swapped each frame tick:
//
//   * Front queue: I/O threads append received messages (MsgQueueInsert), brief lock.
//   * Back queue : the logic thread drains messages (GetMsg).
//   * Flip       : at each logic-frame tick, move Front -> Back (MsgQueueFlip).
//
// The original kept GetMsg lock-free by pinning Flip+GetMsg to a single dedicated
// update thread. In the asio port the update tick runs on the shared io_context
// pool, so all three operations take the same lock — same Front/Back behaviour,
// trivially correct under the pool. (Under _USE_TBB the original swaps the whole
// thing for tbb::concurrent_queue; not needed here.)
class MsgManager {
public:
    // I/O thread: enqueue a newly received message (-> Front).
    void MsgQueueInsert(Message msg) {
        std::lock_guard<std::mutex> lk(m_mx);
        m_front.push_back(std::move(msg));
    }

    // Logic thread: move everything accumulated in Front into Back for processing.
    void MsgQueueFlip() {
        std::lock_guard<std::mutex> lk(m_mx);
        while (!m_front.empty()) {
            m_back.push_back(std::move(m_front.front()));
            m_front.pop_front();
        }
    }

    // Logic thread: pop the next message to process from Back. false when drained.
    bool GetMsg(Message& out) {
        std::lock_guard<std::mutex> lk(m_mx);
        if (m_back.empty()) return false;
        out = std::move(m_back.front());
        m_back.pop_front();
        return true;
    }

    std::size_t FrontCount() const { std::lock_guard<std::mutex> lk(m_mx); return m_front.size(); }
    std::size_t BackCount()  const { std::lock_guard<std::mutex> lk(m_mx); return m_back.size(); }

private:
    mutable std::mutex  m_mx;
    std::deque<Message> m_front;
    std::deque<Message> m_back;
};

}} // namespace sc::net

#endif // SC_NET_MSG_MANAGER_H
