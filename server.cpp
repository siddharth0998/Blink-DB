#include <iostream>
#include <string>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "protocol.h"

using namespace std;

int one_message(int client_fd)
{
    // read what the cleint sent
    string msg;
    int n = recv_msg(client_fd, msg);
    if (n < 0)
    {
        return -1;
    }
    cout << "Client: " << msg << endl;

    // send a reply
    string reply;
    do
    {
        cout << "Server: ";
        getline(cin, reply);
    } while (reply.empty());
    return send_msg(client_fd, reply);
}

int main()
{
    // 1. create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        cerr << "socket() failed" << endl;
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 2. bind
    sockaddr_in addr{};
    addr.sin_family = AF_INET;         // IPv4
    addr.sin_port = htons(1234);       // port
    addr.sin_addr.s_addr = INADDR_ANY; // accept connection to any IP

    if (::bind(server_fd, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        cerr << "bind() failed" << endl;
        return 1;
    }

    // 3. listen
    if (listen(server_fd, SOMAXCONN) < 0)
    {
        cerr << "listen() failed" << endl;
        return 1;
    }

    cout << "Server is running on port " << ntohs(addr.sin_port) << "..." << endl;
    cout << "Waiting for the client..." << endl;

    while (true)
    {
        // 4. accept clients, one by one, forever
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0)
        {
            perror("client couldn't connected.");
            return 1;
        }
        cout << "Client connected! start talking." << endl;

        while (true)
        {
            int n = one_message(client_fd);
            if (n < 0)
            {
                cerr << "Client left the chat." << endl;
                break;
            }
        }
        close(client_fd);
    }
    close(server_fd);
    return 0;
}
