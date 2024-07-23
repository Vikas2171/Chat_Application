#include <iostream>
#include <winsock.h>
#include <fstream>
#include <thread>

#define PORT 9909
#define SIZE 1024

using namespace std;
int nClientSocket;
struct sockaddr_in srv;
char buff[SIZE] = { 0, };
int nRet = 0;

void cleanup() {
    closesocket(nClientSocket);
    WSACleanup();
}

void send_message() {
    memset(buff, 0, sizeof(buff));
    // Text message
    fgets(buff, SIZE, stdin);
    buff[strcspn(buff, "\n")] = '\0';

    nRet = send(nClientSocket, buff, strlen(buff), 0);
    if (nRet == SOCKET_ERROR) {
        int error = WSAGetLastError();
        cout << "send() failed with error: " << error << endl;
        if (error == WSAECONNRESET) {
            cout << "Connection reset by server." << endl;
        }
        cleanup();
        return;
    }
    memset(buff, 0, sizeof(buff));
}

void receive_messages() {
    while (true) {
        memset(buff, 0, SIZE);
        nRet = recv(nClientSocket, buff, SIZE, 0);
            if (nRet <= 0) {
                cout << "Connection closed by server." << endl;
                cleanup();
                exit(EXIT_FAILURE);
            }
            cout << buff << endl;
    }
}

int main() {
    WSADATA ws;
    if (WSAStartup(MAKEWORD(2, 2), &ws) != 0) {
        cout << "WSAStartup failed." << endl;
        return(EXIT_FAILURE);
    }

    nClientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (nClientSocket < 0) {
        cout << "socket() call failed." << endl;
        WSACleanup();
        return (EXIT_FAILURE);
    }

    srv.sin_family = AF_INET;
    srv.sin_port = htons(PORT);
    srv.sin_addr.s_addr = inet_addr("127.0.0.1");
    memset(&srv.sin_zero, 0, 8);

    nRet = connect(nClientSocket, (struct sockaddr*)&srv, sizeof(srv));
    if (nRet < 0) {
        cout << "connect failed." << endl;
        cleanup();
        return (EXIT_FAILURE);
    }
    else {
        cout << "Connected to the server." << endl;

        // Receive initial message from the server
        nRet = recv(nClientSocket, buff, SIZE, 0);
        if (nRet <= 0) {
            cout << "Failed to receive message from server or connection closed." << endl;
            cleanup();
            return EXIT_FAILURE;
        }

        cout << "Message received from the server: " << buff << endl;

        // clearing buffer
        memset(buff, 0, SIZE);

        thread receiver(receive_messages);
        receiver.detach();

        cout << "Type message and hit enter to send it to others..." << endl;
        cout << "*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*" << endl << endl;

        while (true) {
            send_message();
        }
    }

    cleanup();
    return 0;
}
