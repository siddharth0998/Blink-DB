#include <iostream>
#include <string>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "protocol.h"

using namespace std;

int main()
{
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

    while(true){
        // 4. send a msg
        string msg;
        cout << "Client: ";
        getline(cin, msg);

        if (send_msg(fd, msg) < 0){
            cerr << "Failed to send." << endl;
            break;
        }

        // 5. read the reply
        string reply;
        if(recv_msg(fd,reply) < 0){
            cerr << "Server left the chat." << endl;
            break;
        }
        cout << "Server: " << reply << endl;
    }

    // 6. close the connection
    close(fd);
    return 0;
}
