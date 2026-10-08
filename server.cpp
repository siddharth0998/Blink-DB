#include <iostream>
#include <string>
#include <vector>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "protocol.h"

using namespace std;

struct Conn
{
    int fd = -1;
    bool want_read = false;
    bool want_write = false;
    bool want_close = false;
    string incoming;
    string outgoing;
};

void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
    {
        perror("fcntl get failed");
        exit(1);
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        perror("fcntl set failed");
        exit(1);
    }
}

Conn *handle_accept(int server_fd)
{
    sockaddr_in client_addr{};
    socklen_t len = sizeof(client_addr);
    int client_fd = accept(server_fd, (sockaddr *)&client_addr, &len);
    if (client_fd < 0)
    {
        if (errno != EAGAIN)
        {
            perror("accept failed");
        }
        return nullptr;
    }

    cout << "New client " << client_fd << " from "
         << inet_ntoa(client_addr.sin_addr) << ":"
         << ntohs(client_addr.sin_port) << endl;

    set_nonblocking(client_fd);

    Conn *conn = new Conn();
    conn->fd = client_fd;
    conn->want_read = true;
    return conn;
}

bool try_one_message(Conn *conn)
{
    if (conn->incoming.size() < 4)
    {
        return false;
    }

    uint32_t len = 0;
    memcpy(&len, &conn->incoming[0], 4);

    if (len > MAX_MSG)
    {
        cerr << "Client " << conn->fd << ": message too long" << endl;
        conn->want_close = true;
        return false;
    }

    if (conn->incoming.size() < 4 + len)
    {
        return false;
    }

    string msg = conn->incoming.substr(4, len);
    cout << "Client " << conn->fd << ": " << msg << endl;
    conn->outgoing += make_packet(msg);
    conn->incoming.erase(0, 4 + len);
    return true;
}

void handle_write(Conn *conn)
{
    ssize_t rv = write(conn->fd, conn->outgoing.data(), conn->outgoing.size());

    if (rv < 0 && errno == EAGAIN)
    {
        return;
    }

    if (rv < 0)
    {
        perror("write failed");
        conn->want_close = true;
        return;
    }
    conn->outgoing.erase(0, rv);
    if (conn->outgoing.empty())
    {
        conn->want_write = false;
        conn->want_read = true;
    }
}

void handle_read(Conn *conn)
{
    char buf[64 * 1024];
    ssize_t rv = read(conn->fd, buf, sizeof(buf));

    if (rv < 0 && errno == EAGAIN)
    {
        return;
    }

    if (rv < 0)
    {
        perror("read failed");
        conn->want_close = true;
        return;
    }
    if (rv == 0)
    {
        cout << "Client " << conn->fd << " left the chat." << endl;
        conn->want_close = true;
        return;
    }

    conn->incoming.append(buf, rv);

    while (try_one_message(conn))
        ;

    if (!conn->outgoing.empty())
    {
        conn->want_read = false;
        conn->want_write = true;
        handle_write(conn);
    }
}

int main()
{
    signal(SIGPIPE, SIG_IGN);

    // 1. create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket failed");
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
        perror("bind failed");
        return 1;
    }

    // set to non_blocking mode
    set_nonblocking(server_fd);

    // 3. listen
    if (listen(server_fd, SOMAXCONN) < 0)
    {
        perror("listen failed");
        return 1;
    }

    cout << "Server is running on port " << ntohs(addr.sin_port) << "..." << endl;

    vector<Conn *> fd2conn;
    vector<pollfd> poll_args;

    while (true)
    {
        poll_args.clear();
        poll_args.push_back({server_fd, POLLIN, 0});

        for (Conn *conn : fd2conn)
        {
            if (conn == nullptr)
                continue;
            pollfd pfd = {conn->fd, POLLERR, 0};
            if (conn->want_read)
                pfd.events |= POLLIN;
            if (conn->want_write)
                pfd.events |= POLLOUT;
            poll_args.push_back(pfd);
        }

        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1);
        if (rv < 0 && errno == EINTR)
            continue;
        if (rv < 0)
        {
            perror("poll failed");
            return 1;
        }

        if (poll_args[0].revents)
        {
            Conn *conn = handle_accept(server_fd);
            if (conn != nullptr)
            {
                if (fd2conn.size() <= (size_t)conn->fd)
                {
                    fd2conn.resize(conn->fd + 1, nullptr);
                }
                fd2conn[conn->fd] = conn;
            }
        }

        for (size_t i = 1; i < poll_args.size(); i++)
        {
            short ready = poll_args[i].revents;
            if (ready == 0)
            {
                continue;
            }

            Conn *conn = fd2conn[poll_args[i].fd];

            if (ready & POLLIN)
                handle_read(conn);
            if (ready & POLLOUT)
                handle_write(conn);

            bool hung_up = (ready & POLLHUP) && !(ready & POLLIN);

            if ((ready & POLLERR) || hung_up || conn->want_close)
            {
                close(conn->fd);
                fd2conn[conn->fd] = nullptr;
                delete conn;
            }
        }
    }
    close(server_fd);
    return 0;
}
