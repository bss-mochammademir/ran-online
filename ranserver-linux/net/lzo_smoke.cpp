#include "minlzo.h"
#include "send_msg_buffer.h"
#include "packet.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <cstring>

using namespace sc::net;

void test_raw_lzo() {
    std::cout << "Running test_raw_lzo..." << std::endl;
    CMinLzo& lzo = CMinLzo::GetInstance();
    int r = lzo.init();
    assert(r == CMinLzo::MINLZO_SUCCESS);

    std::string original = "The quick brown fox jumps over the lazy dog. 1234567890! "
                           "The quick brown fox jumps over the lazy dog. 1234567890! "
                           "The quick brown fox jumps over the lazy dog. 1234567890! "
                           "The quick brown fox jumps over the lazy dog. 1234567890!";
    
    std::vector<uint8_t> compBuf(original.size() * 2);
    int compLen = compBuf.size();

    r = lzo.lzoCompress(
        reinterpret_cast<const uint8_t*>(original.data()),
        original.size(),
        compBuf.data(),
        compLen
    );
    assert(r == CMinLzo::MINLZO_SUCCESS);
    assert(compLen < static_cast<int>(original.size())); // must compress

    std::vector<uint8_t> decompBuf(original.size());
    int decompLen = decompBuf.size();

    r = lzo.lzoDeCompress(
        compBuf.data(),
        compLen,
        decompBuf.data(),
        decompLen
    );
    assert(r == CMinLzo::MINLZO_SUCCESS);
    assert(decompLen == static_cast<int>(original.size()));
    assert(std::memcmp(original.data(), decompBuf.data(), decompLen) == 0);

    std::cout << "test_raw_lzo PASSED" << std::endl;
}

void test_send_and_frame_compressed() {
    std::cout << "Running test_send_and_frame_compressed..." << std::endl;
    SendMsgBuffer sendBuf;
    
    // We will add multiple framed messages with highly compressible repeating data
    std::string payload1(600, 'A');
    std::string payload2(600, 'B');
    
    std::vector<char> sourceMsg1 = EncodeMessage(101, payload1.data(), payload1.size());
    std::vector<char> sourceMsg2 = EncodeMessage(102, payload2.data(), payload2.size());
    
    // Aggregate them, should return BUFFER_ADDED
    int r = sendBuf.addMsg(sourceMsg1.data(), sourceMsg1.size());
    assert(r == SendMsgBuffer::BUFFER_ADDED);
    
    // Total size will now be 608 + 608 = 1216 (>= 1000), should trigger BUFFER_SEND / BUFFER_SEND_ADD
    r = sendBuf.addMsg(sourceMsg2.data(), sourceMsg2.size());
    assert(r == SendMsgBuffer::BUFFER_SEND || r == SendMsgBuffer::BUFFER_SEND_ADD);

    int sendSize = sendBuf.getSendSize();
    assert(sendSize > 0);

    const uint8_t* wireData = sendBuf.getSendBuffer();
    const NET_COMPRESS* header = reinterpret_cast<const NET_COMPRESS*>(wireData);
    
    assert(header->nType == NET_MSG_COMPRESS);
    assert(header->dwSize == static_cast<uint32_t>(sendSize));
    assert(header->bCompress == 1); // should be compressed now!

    // Now feed the compressed wire bytes to the MessageFramer
    MessageFramer framer;
    std::vector<Message> outMsgs;
    bool ok = framer.Feed(reinterpret_cast<const char*>(wireData), sendSize, outMsgs);
    assert(ok);

    // If we aggregated successfully, we should recover the messages
    assert(outMsgs.size() >= 1);
    assert(outMsgs[0].type == 101);
    assert(outMsgs[0].payloadSize() == 600);
    assert(std::memcmp(outMsgs[0].payload(), payload1.data(), 600) == 0);

    std::cout << "test_send_and_frame_compressed PASSED" << std::endl;
}

void test_send_and_frame_uncompressed() {
    std::cout << "Running test_send_and_frame_uncompressed..." << std::endl;
    
    // We construct a mock uncompressed NET_COMPRESS packet
    // payload: two normal messages
    std::vector<char> msg1 = EncodeMessage(201, "Alpha", 5);
    std::vector<char> msg2 = EncodeMessage(202, "Beta", 4);
    
    uint32_t payloadSize = msg1.size() + msg2.size();
    uint32_t wireSize = sizeof(NET_COMPRESS) + payloadSize;
    
    std::vector<char> wire(wireSize);
    NET_COMPRESS* header = reinterpret_cast<NET_COMPRESS*>(wire.data());
    header->dwSize = wireSize;
    header->nType = NET_MSG_COMPRESS;
    header->bCompress = 0; // NOT compressed
    std::memset(header->pad, 0, sizeof(header->pad));

    std::memcpy(wire.data() + sizeof(NET_COMPRESS), msg1.data(), msg1.size());
    std::memcpy(wire.data() + sizeof(NET_COMPRESS) + msg1.size(), msg2.data(), msg2.size());

    // Feed to framer
    MessageFramer framer;
    std::vector<Message> outMsgs;
    bool ok = framer.Feed(wire.data(), wire.size(), outMsgs);
    assert(ok);

    assert(outMsgs.size() == 2);
    assert(outMsgs[0].type == 201);
    assert(outMsgs[0].payloadSize() == 5);
    assert(std::memcmp(outMsgs[0].payload(), "Alpha", 5) == 0);

    assert(outMsgs[1].type == 202);
    assert(outMsgs[1].payloadSize() == 4);
    assert(std::memcmp(outMsgs[1].payload(), "Beta", 4) == 0);

    std::cout << "test_send_and_frame_uncompressed PASSED" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Starting LZO Compression Smoke Tests..." << std::endl;
    std::cout << "========================================" << std::endl;

    test_raw_lzo();
    test_send_and_frame_compressed();
    test_send_and_frame_uncompressed();

    std::cout << "========================================" << std::endl;
    std::cout << "All LZO Smoke Tests PASSED!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
