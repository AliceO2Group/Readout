// server to implement simple ZMQ REQ/REPLY synchronization barrier
// all clients with same syncId will get a (almost) synchronous after a timeout, which triggers after no new client with this id connects

#include <zmq.h>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <cstring>
#include <algorithm>

#include <InfoLogger/InfoLogger.hxx>
#include <InfoLogger/InfoLoggerMacros.hxx>

// definition of a global for logging
using namespace AliceO2::InfoLogger;
InfoLogger theLog;


using namespace std;
using namespace std::chrono;

struct Client {
    zmq_msg_t identity;
};

struct Group {
    vector<Client> clients;
    time_point<steady_clock> lastActivity;
    bool waiting = false;
};

unordered_map<int, Group> groups;
mutex groups_mutex;
void* router_socket = nullptr;

void send_reply_to_group(int syncId) {
    vector<Client> clientsToReply;

    {
        lock_guard lock(groups_mutex);
        auto& group = groups[syncId];
        clientsToReply = move(group.clients);
        group.clients.clear();
        group.waiting = false;
    }

    for (auto& client : clientsToReply) {
        zmq_msg_send(&client.identity, router_socket, ZMQ_SNDMORE);
        zmq_msg_close(&client.identity);

        zmq_msg_t empty;
        zmq_msg_init_size(&empty, 0);
        zmq_msg_send(&empty, router_socket, ZMQ_SNDMORE);
        zmq_msg_close(&empty);

	std::string reply = "SYNC for id " + std::to_string(syncId);
        zmq_msg_t msg;
        zmq_msg_init_size(&msg, reply.length());
        memcpy(zmq_msg_data(&msg), reply.c_str(), reply.length());
        zmq_msg_send(&msg, router_socket, 0);
        zmq_msg_close(&msg);
    }

    theLog.log(LogInfoDevel, "SYNC for id %d sent to %d clients", syncId, (int) clientsToReply.size());
}


void start_group_timer(int syncId) {
    std::thread([syncId]() {
        using namespace std::chrono;
        constexpr auto timeout = seconds(5);

        while (true) {
            time_point<steady_clock> last;

            {
                std::lock_guard lock(groups_mutex);
                last = groups[syncId].lastActivity;
            }

            auto now = steady_clock::now();
            auto elapsed = now - last;

            if (elapsed >= timeout) {
                send_reply_to_group(syncId);
                break;
            }

            auto remaining = timeout - elapsed;
            auto sleep_duration = std::min(duration_cast<milliseconds>(remaining), milliseconds(500));
            std::this_thread::sleep_for(sleep_duration);
        }
    }).detach();
}



int main() {
    void* context = zmq_ctx_new();
    router_socket = zmq_socket(context, ZMQ_ROUTER);
    const char *address = "tcp://*:50003";
    zmq_bind(router_socket, address);

    theLog.setContext(InfoLoggerContext({ { InfoLoggerContext::FieldName::Facility, (std::string) "readout/sync" } }));

    theLog.log(LogInfoDevel, "readout SYNC server started on %s", address);

    while (true) {
        zmq_msg_t identity;
        zmq_msg_init(&identity);
        zmq_msg_recv(&identity, router_socket, 0);

        zmq_msg_t empty;
        zmq_msg_init(&empty);
        zmq_msg_recv(&empty, router_socket, 0);
        zmq_msg_close(&empty);

        zmq_msg_t message;
        zmq_msg_init(&message);
        zmq_msg_recv(&message, router_socket, 0);

        string msg_str((char*)zmq_msg_data(&message), zmq_msg_size(&message));
        int syncId = stoi(msg_str);
        zmq_msg_close(&message);

        lock_guard lock(groups_mutex);
        auto& group = groups[syncId];

        Client c;
        zmq_msg_init(&c.identity);
        zmq_msg_copy(&c.identity, &identity);
        group.clients.push_back(move(c));
        group.lastActivity = steady_clock::now();

	theLog.log(LogInfoDevel, "New client waiting for sync id %d", syncId);

        if (!group.waiting) {
            group.waiting = true;
            start_group_timer(syncId);
        }

        zmq_msg_close(&identity);
    }

    zmq_close(router_socket);
    zmq_ctx_term(context);
    return 0;
}
