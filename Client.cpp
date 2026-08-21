#include <iostream>
#include <winsock.h>
#include <thread>
#include <atomic>

#include "Framing.h"

#define PORT 9909
#define SIZE 1024

// winsock.h (Winsock 1.1) doesn't declare SD_BOTH/SD_SEND/SD_RECEIVE the way
// winsock2.h does; 2 is SD_BOTH's underlying value on Windows.
#define SHUTDOWN_BOTH 2

using namespace std;

static SOCKET g_clientSocket = INVALID_SOCKET;
static atomic<bool> g_running{ true };
static atomic<bool> g_shuttingDown{ false };

// Single entry point for tearing the connection down, safe to call from
// either thread (send_message()'s error path or receive_messages()'s error
// path). Only the first caller actually closes the socket - this both
// prevents a double closesocket()/WSACleanup() race and is what unblocks
// whichever thread is currently blocked in recv()/send() on this socket.
static void triggerShutdown() {
    bool expected = false;
    if (g_shuttingDown.compare_exchange_strong(expected, true)) {
        g_running = false;
        shutdown(g_clientSocket, SHUTDOWN_BOTH);
        closesocket(g_clientSocket);
    }
}

static void send_message() {
    char buff[SIZE] = { 0 };

    if (!fgets(buff, SIZE, stdin)) {
        // EOF (Ctrl+Z / Ctrl+D) or a stdin read error - stop sending instead
        // of spinning on an immediately-returning fgets() forever.
        cout << "Input closed. Disconnecting..." << endl;
        triggerShutdown();
        return;
    }
    buff[strcspn(buff, "\n")] = '\0';

    if (strcmp(buff, "quit") == 0) {
        cout << "Disconnecting..." << endl;
        triggerShutdown();
        return;
    }

    string framed = string(buff) + "\n"; // the delimiter this protocol frames messages on
    int nRet = send(g_clientSocket, framed.c_str(), (int)framed.size(), 0);
    if (nRet == SOCKET_ERROR) {
        if (g_running) { // only report if this wasn't already an expected shutdown
            int error = WSAGetLastError();
            cout << "send() failed with error: " << error << endl;
        }
        triggerShutdown();
    }
}

static void receive_messages() {
    char buff[SIZE];
    LineFramer framer;
    while (g_running) {
        int nRet = recv(g_clientSocket, buff, SIZE, 0);
        if (nRet <= 0) {
            if (g_running) { // only report if this wasn't already an expected shutdown
                cout << "Connection closed by server." << endl;
            }
            triggerShutdown();
            break;
        }
        // A single recv() can contain a partial message, several messages
        // back-to-back, or both - the framer reassembles the newline-
        // delimited messages the server actually sends.
        for (const string& line : framer.feed(buff, nRet)) {
            cout << line << endl;
        }
    }
}

int main() {
    WSADATA ws;
    if (WSAStartup(MAKEWORD(2, 2), &ws) != 0) {
        cout << "WSAStartup failed." << endl;
        return EXIT_FAILURE;
    }

    g_clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_clientSocket == INVALID_SOCKET) {
        cout << "socket() call failed." << endl;
        WSACleanup();
        return EXIT_FAILURE;
    }

    struct sockaddr_in srv;
    srv.sin_family = AF_INET;
    srv.sin_port = htons(PORT);
    srv.sin_addr.s_addr = inet_addr("127.0.0.1");
    memset(&srv.sin_zero, 0, 8);

    if (connect(g_clientSocket, (struct sockaddr*)&srv, sizeof(srv)) == SOCKET_ERROR) {
        cout << "connect failed." << endl;
        closesocket(g_clientSocket);
        WSACleanup();
        return EXIT_FAILURE;
    }
    cout << "Connected to the server." << endl;

    LineFramer welcomeFramer;
    vector<string> welcomeLines;
    while (welcomeLines.empty()) {
        char welcomeBuff[SIZE];
        int nRet = recv(g_clientSocket, welcomeBuff, SIZE, 0);
        if (nRet <= 0) {
            cout << "Failed to receive message from server or connection closed." << endl;
            closesocket(g_clientSocket);
            WSACleanup();
            return EXIT_FAILURE;
        }
        welcomeLines = welcomeFramer.feed(welcomeBuff, nRet);
    }
    cout << "Message received from the server: " << welcomeLines[0] << endl;

    thread receiver(receive_messages);

    cout << "Type message and hit enter to send it to others..." << endl;
    cout << "Type \"quit\" and press Enter to disconnect." << endl;
    cout << "*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*" << endl << endl;

    while (g_running) {
        send_message();
    }

    // triggerShutdown() already closed the socket by this point (from
    // whichever side noticed the disconnect first), so recv() in the
    // receiver thread is already unblocked and it's safe to join here.
    receiver.join();

    WSACleanup();
    return 0;
}
