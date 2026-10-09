#include <iostream>
#include <string>
#include <sstream>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "protocol.h"

using namespace std;

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

int send_msg(int fd, const string &body)
{
    if (body.size() > MAX_MSG)
    {
        cerr << "message too long, not sent" << endl;
        return -1;
    }
    string packet = make_packet(body);
    return write_full(fd, packet.data(), packet.size());
}

int recv_msg(int fd, string &body)
{
    uint32_t len = 0;
    if (read_full(fd, (char *)&len, 4) < 0)
        return -1;

    if (len > MAX_MSG)
    {
        cerr << "message too long." << endl;
        return -1;
    }

    body.resize(len);
    if (read_full(fd, &body[0], len) < 0)
        return -1;
    return 0;
}

int send_request(int fd, const vector<string> &cmd)
{
    return send_msg(fd, make_request_body(cmd));
}

int recv_response(int fd, uint32_t &status, string &data)
{
    string body;
    if (recv_msg(fd, body) < 0)
    {
        return -1;
    }
    if (!parse_response(body, status, data))
    {
        cerr << "bad response" << endl;
        return -1;
    }
    return 0;
}

string status_text(uint32_t status)
{
    if (status == RES_OK)
        return "ok";
    if (status == RES_ERR)
        return "ERROR (uknown command)";
    if (status == RES_NX)
        return "NOT FOUND";
    return "UNKNOW STATUS " + to_string(status);
}

int run_command(int fd, const vector<string> &cmd)
{
    if (send_request(fd, cmd) < 0)
    {
        cout << "Failed to send." << endl;
        return -1;
    }

    uint32_t status = 0;
    string data;
    if (recv_response(fd, status, data) < 0)
    {
        cout << "Server left or sent a bad response." << endl;
        return -1;
    }

    cout << status_text(status);
    if (!data.empty())
    {
        cout << ": " << data;
    }
    cout << endl;
    return 0;
}

vector<string> split_words(const string &line)
{
    vector<string> words;
    istringstream in(line);
    string word;
    while (in >> word)
    {
        words.push_back(word);
    }
    return words;
}

int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);
    // 1. create a socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        perror("socket failed");
        return 1;
    }

    // 2. say which server to connect
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);                   // server's port
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.0

    // 3. connect
    if (connect(fd, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("connect failed");
        return 1;
    }

    if (argc > 1)
    {
        vector<string> cmd;
        for (int i = 1; i < argc; i++)
        {
            cmd.push_back(argv[i]);
        }
        int err = run_command(fd, cmd);
        close(fd);
        if (err < 0)
        {
            return 1;
        }
        return 0;
    }

    cout << "Connected! Try: set name sid | get name | del name" << endl;
    cout << "Type 'quit' to leave." << endl;

    while (true)
    {
        cout << "> ";
        string line;
        if (!getline(cin, line))
        {
            break;
        }
        vector<string> cmd = split_words(line);
        if (cmd.empty())
            continue;
        if (cmd.size() == 1 && cmd[0] == "quit")
            break;
        if (run_command(fd, cmd) < 0)
            break;
    }

    // 6. close the connection
    close(fd);
    return 0;
}
