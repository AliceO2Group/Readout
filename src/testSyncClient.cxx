// client to test a simple ZMQ REQ/REPLY synchronization barrier

#include <zmq.h>
#include <iostream>
#include <string>
#include <cstring>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: o2-readout-test-sync-client <syncId>\n";
        return 1;
    }

    int syncId = std::stoi(argv[1]);
    std::string msg = std::to_string(syncId);

    void* context = zmq_ctx_new();
    void* socket = zmq_socket(context, ZMQ_REQ);
    zmq_connect(socket, "tcp://localhost:50003");

    int timeout = 7000; // ms
    zmq_setsockopt(socket, ZMQ_SNDTIMEO, &timeout, sizeof(timeout)); // for zmq_send
    zmq_setsockopt(socket, ZMQ_RCVTIMEO, &timeout, sizeof(timeout)); // for zmq_recv

    zmq_send(socket, msg.c_str(), msg.size(), 0);

    char buffer[256];
    int size = zmq_recv(socket, buffer, sizeof(buffer) - 1, 0);

    if (size == -1) {
        std::cout << "Timeout: No reply within " << timeout << " ms\n";
    } else {
        buffer[size] = '\0';
        std::cout << "Reply: " << buffer << "\n";
    }

    zmq_close(socket);
    zmq_ctx_term(context);
    return 0;
}
