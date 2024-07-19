#include <iostream>
#include <queue>
#include <winsock.h>
#include <fstream>


#define SIZE 1024
using namespace std;

#define PORT 9909
struct sockaddr_in srv;
fd_set fr, fw, fe;
int nMaxFd;
int nSocket;

char buff[SIZE] = { 0, };
int ArrClient[5] = { 0 };     // To handle multiple clients
queue<int> pendingConnections;  // Queue to hold pending connections

// Function prototypes
void ProcessNewMessage(int nClientSocket);
void ProcessNewRequest(bool isPendingRequest);
void recv_text_msg(int nClientSocket, int nRet);
void client_gone(int nClientSocket);
void broadcast_message(const char* msg, int sender);

void broadcast_message(const char* msg, int sender) {
    for (int i = 0; i < 5; i++) {
        if (ArrClient[i] == sender) continue;
        send(ArrClient[i], msg, strlen(msg), 0);
    }
}

void client_gone(int nClientSocket) {
    closesocket(nClientSocket);
    for (int index = 0; index < 5; index++) {
        if (ArrClient[index] == nClientSocket) {
            ArrClient[index] = 0;
            break;
        }
    }
    // Try to accept a pending connection if available
    if (!pendingConnections.empty()) {
        int pendingSocket = pendingConnections.front();
        //pendingConnections.pop();
        ProcessNewRequest(true);  // Process the pending request
    }
}

void recv_text_msg(int nClientSocket, int nRet) {
    cout << "Processing the new message for client socket: " << nClientSocket << endl;
    if (nRet == SOCKET_ERROR) {
        int error = WSAGetLastError();
        cout << "recv() failed with error: " << error << endl;
        if (error == WSAECONNRESET) {
            cout << "Connection reset by client." << endl;
            cout << endl << "*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*" << endl << endl;
        }
        client_gone(nClientSocket);
    }
    else if (nRet == 0) {
        // Connection has been gracefully closed
        cout << "Connection closed by client." << endl;
        client_gone(nClientSocket);
    }
    else {
        // directly sending message to all clients without saving or showing to the server
        // cout << "The message received from client is: " << buff;
        broadcast_message(buff, nClientSocket);
        memset(buff, 0, SIZE);
        cout << endl << "*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*" << endl << endl;
    }
}

void ProcessNewMessage(int nClientSocket) {
    memset(buff, 0, SIZE);
    int nRet = recv(nClientSocket, buff, SIZE, 0);
    if (nRet == SOCKET_ERROR) {
        int error = WSAGetLastError();
        cout << "recv() failed with error: " << error << endl;
        if (error == WSAECONNRESET) {
            cout << "Connection reset by client." << endl;
            cout << endl << "*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*" << endl << endl;
        }
        client_gone(nClientSocket);
    }
    else if (nRet == 0) {
        // Connection has been gracefully closed
        cout << "Connection closed by client." << endl;
        client_gone(nClientSocket);
    }
    else {
        buff[strcspn(buff, "\n")] = '\0';
        recv_text_msg(nClientSocket, nRet);
    }
}

void ProcessNewRequest(bool isPendingRequest = 0) {
    int clientSocket;
    if (isPendingRequest) {
        clientSocket = pendingConnections.front();
        pendingConnections.pop();
    }
    else if (FD_ISSET(nSocket, &fr)) {
        // New connection request
        int nLen = sizeof(struct sockaddr);
        clientSocket = accept(nSocket, NULL, &nLen);
        if (clientSocket < 0) {
            return; // If the accept call fails, exit the function
        }
    }
    else {
        // Check for messages from existing clients
        for (int index = 0; index < 5; index++) {
            if (ArrClient[index] != 0 && FD_ISSET(ArrClient[index], &fr)) {
                ProcessNewMessage(ArrClient[index]);
            }
        }
        return; // Exit the function after processing messages from existing clients
    }

    // Check for space availability
    bool spaceAvailable = false;
    for (int index = 0; index < 5; index++) {
        if (ArrClient[index] == 0) {
            ArrClient[index] = clientSocket;
            send(clientSocket, "Got the connection done successfully.", 38, 0);
            spaceAvailable = true;
            break;
        }
    }
    if (!spaceAvailable) {
        cout << "New connection is pending." << endl;
        cout << endl << "*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*" << endl << endl;
        // Send message to client indicating no space
        send(clientSocket, "No space available on the server. Your connection is pending.", 61, 0);
        pendingConnections.push(clientSocket);
    }
}

int main() {
    int nRet = 0;
    // Initialize the WSA variables
    WSADATA ws;
    if (WSAStartup(MAKEWORD(2, 2), &ws) == 0) {
        cout << "WSA initialized." << endl;
    }
    else {
        cout << "WSA initialization failed." << endl;
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    // Initialize the socket
    nSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (nSocket < 0) {
        cout << "Socket not opened." << endl;
        WSACleanup();
        exit(EXIT_FAILURE);
    }
    else {
        cout << "Socket opened successfully. \nSocket Descriptor : " << nSocket << endl;
    }

    // Initialize the environment for sockaddr structure
    srv.sin_family = AF_INET;
    srv.sin_port = htons(PORT);
    srv.sin_addr.s_addr = INADDR_ANY;
    memset(&(srv.sin_zero), 0, 8);

    // setsockopt
    int nOptVal = 1;
    int nOptLen = sizeof(nOptVal);
    nRet = setsockopt(nSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&nOptVal, nOptLen);
    if (nRet == 0) {
        cout << "The setsockopt call successfull." << endl;
    }
    else {
        cout << "The setsockopt call failed." << endl;
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    // Bind the socket to the local port
    nRet = bind(nSocket, (sockaddr*)&srv, sizeof(sockaddr));
    if (nRet == 0) {
        cout << "Successfully bind to local port." << endl;
    }
    else {
        cout << "Failed to bind to local port." << endl;
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    // Listen the request from client (queues the requests)
    nRet = listen(nSocket, 5);
    if (nRet == 0) {
        cout << "Started listening to local port." << endl;
    }
    else {
        cout << "Failed to start listen to local port." << endl;
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    while (1) {
        FD_ZERO(&fr);
        FD_ZERO(&fw);
        FD_ZERO(&fe);

        FD_SET(nSocket, &fr);
        FD_SET(nSocket, &fe);

        nMaxFd = nSocket; // Reset nMaxFd to nSocket initially

        for (int index = 0; index < 5; index++) {
            if (ArrClient[index] != 0) {
                FD_SET(ArrClient[index], &fr);
                FD_SET(ArrClient[index], &fe);
                if (ArrClient[index] > nMaxFd) {
                    nMaxFd = ArrClient[index]; // Update nMaxFd if a client socket is greater
                }
            }
        }

        nRet = select(nMaxFd + 1, &fr, &fw, &fe, &tv);
        if (nRet > 0) {
            cout << "Data on port....Processing now...." << endl;
            ProcessNewRequest();
        }
        else if (nRet == 0) {
            // Nothing on port
        }
        else {
            cout << "select call failed." << endl;
            WSACleanup();
            exit(EXIT_FAILURE);
        }
    }
}
