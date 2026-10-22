#pragma once
#include <stdint.h>
#include <stddef.h>

enum MsgStatus : uint8_t { MSG_RECEIVED = 0, MSG_QUEUED, MSG_SENT, MSG_RELAYED, MSG_ACKED, MSG_FAILED };

struct Message {
    uint32_t from, to, id;
    uint32_t time;          // epoch seconds (0 if clock unknown)
    uint8_t status;
    bool pki;
    bool unread;
    char text[234];
};

namespace messages {

static const size_t CAPACITY = 300;

void load();
void save_if_dirty();
Message *add(const Message &m);
size_t count();
Message *at(size_t i);                  // 0 = oldest
Message *find_sent(uint32_t id);
void set_status(uint32_t id, MsgStatus s);
uint32_t version();
size_t unread_count();
// Conversation key: BROADCAST for the channel, else the peer node number
uint32_t peer_of(const Message &m, uint32_t our_num);
void mark_read(uint32_t peer, uint32_t our_num);
size_t unread_for(uint32_t peer, uint32_t our_num);

} // namespace messages
