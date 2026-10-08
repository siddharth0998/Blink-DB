#pragma once

#include <string>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <unistd.h>

using namespace std;

const size_t MAX_MSG = 4096;

int read_full(int fd, char *buf, size_t n)
{
    while (n > 0)
    {
        ssize_t rv = read(fd, buf, n);
        if (rv <= 0)
            return -1;
        n -= rv;
        buf += rv;
    }
    return 0;
}

int write_full(int fd, const char *buf, size_t n)
{
    while (n > 0)
    {
        ssize_t rv = write(fd, buf, n);
        if (rv <= 0)
            return -1;
        n -= rv;
        buf += rv;
    }
    return 0;
}

string make_packet(const string &msg)
{
    uint32_t len = msg.size();
    string packet(4, '\0');
    memcpy(&packet[0], &len, 4);
    packet += msg;
    return packet;
}

int send_msg(int fd, const string &msg)
{
    if (msg.size() > MAX_MSG)
    {
        cerr << "message too long, not sent" << endl;
        return -1;
    }
    string packet = make_packet(msg);
    return write_full(fd, packet.data(), packet.size());
}

int recv_msg(int fd, string &msg)
{
    uint32_t len = 0;
    if (read_full(fd, (char *)&len, 4) < 0)
        return -1;

    if (len > MAX_MSG)
    {
        cerr << "message too long." << endl;
        return -1;
    }

    msg.resize(len);
    if (read_full(fd, &msg[0], len) < 0)
        return -1;
    return 0;
}
