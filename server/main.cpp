#include <iostream>
#include <winsock2.h>
#include <getopt.h>
using namespace std;

#define LISTENQ 10

u_short _server_port = 2800;
int _cpu_set = 0;

SOCKET gClientSocket[FD_SETSIZE] = {0};
BOOL  gTerminal = FALSE;

unsigned long  WINAPI WorkerThread(void *p);

/*
 * 服务端主函数
 *
 * return
 *          ==0 正常退出
 *          < 0 非正常退出
 */
int _server_main() {
    WSADATA wsadata;
    SOCKET serverfd, clientfd;
    struct sockaddr_in serveraddr, clientaddr;
    HANDLE hHandle = NULL;
    int sin_size = sizeof(SOCKADDR);
    unsigned long arg = 1;
    char szMsg[128] = {""};
    int ret = 0;
    int i = 0;

    if(WSAStartup(MAKEWORD(2,2),&wsadata)==SOCKET_ERROR)
    {
        printf("WSAStartup() failed! lasterr = %d\n", WSAGetLastError());
        return -1;
    }
    serverfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverfd == INVALID_SOCKET) {
        printf("create socket failed! lasterr = %d\n", WSAGetLastError());
        WSACleanup();
        return -1;
    }

    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(_server_port);
    serveraddr.sin_addr.S_un.S_addr = INADDR_ANY;
    ret = bind(serverfd, (SOCKADDR *) &serveraddr, sizeof(SOCKADDR));
    if (ret == -1) {
        printf("bind server socket failed! lasterr = %d\n", WSAGetLastError());
        closesocket(serverfd);
        WSACleanup();
        return  -1;
    }

    ret = listen(serverfd, LISTENQ);
    if (ret == -1) {
        printf("listen server failed! lasterr = %d\n", WSAGetLastError());
        closesocket(serverfd);
        WSACleanup();
        return -1;
    }

    /*
    ret = ioctlsocket(serverfd, FIONBIO, &arg);
    if (ret != NO_ERROR) {
        printf("ioctlsocket FIONBIO failed! lasterr = %d\n", WSAGetLastError());
    } */

    hHandle = CreateThread(NULL, 0, WorkerThread, NULL, 0, 0);
    if (hHandle != NULL) {
        CloseHandle(hHandle);
    } else {
        printf("Create thread failed! lasterr = %d\n", WSAGetLastError());
        closesocket(serverfd);
        WSACleanup();
        return -1;
    }

    while (!gTerminal) {
        printf("waiting for connect ...\n");
        clientfd = accept(serverfd, (SOCKADDR *)&clientaddr, &sin_size);
        if (clientfd == SOCKET_ERROR) {
            printf("accept link failed! lasterr = %d\n", WSAGetLastError());
            closesocket(serverfd);
            WSACleanup();
            return -1;
        } else {
            printf("accept link from %s:%d\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
            for (i = 0; i< FD_SETSIZE; i++) {
                if (gClientSocket[i] <= 0) {
                    gClientSocket[i] = clientfd;
                    break;
                }
            }

            if (FD_SETSIZE == i) {
                printf("The link number beyond the max capacity!\n");
                closesocket(clientfd);
            }
        }

    }

    closesocket(serverfd);
    WSACleanup();

    std::cout<<"server exit!"<<endl;
    return 0;
}

unsigned long  WINAPI WorkerThread(void *p) {
    FD_SET fdRead;
    struct timeval outTime = {1, 0};
    char szMessage[1024] = {0};
    int i = 0;

    while(true) {
        FD_ZERO(&fdRead);
        for (i = 0; i < FD_SETSIZE; i++) {
            if (gClientSocket[i] > 0) {
                FD_SET(gClientSocket[i], &fdRead);
            }
        }
        int iRet = select(0, &fdRead, NULL, NULL, &outTime);
        if(iRet == 0) {
            // time out
            continue;
        } else if (iRet == SOCKET_ERROR) {
            if (fdRead.fd_count == 0) {
                continue;
            }
            // error
            printf("select failed! lasterr:%d\n", WSAGetLastError());
            break;
        } else {
            for (i = 0; i < FD_SETSIZE; i++) {
                if (FD_ISSET(gClientSocket[i], &fdRead)) {
                    int iRecvLen = recv(gClientSocket[i], szMessage, sizeof(szMessage), 0);
                    if (iRecvLen == 0) {
                        printf("Connection closed ! \n");
                        closesocket(gClientSocket[i]);
                        gClientSocket[i] = 0;
                    } else if (iRecvLen < 0) {
                        int nLastError = WSAGetLastError();
                        if (nLastError == WSAECONNRESET || nLastError == WSAECONNABORTED) {
                            printf("Client %d has lost connected: %d\n", gClientSocket[i], nLastError);
                        } else {
                            printf("Client %d recv failed: %d\n", gClientSocket[i], nLastError);
                        }
                        closesocket(gClientSocket[i]);
                        gClientSocket[i] = 0;
                    }
                    else {
                            //printf("Server receive %d message %s:\n", gClientSocket[i], szMessage);
                    }
                }
            }
        }
    }

    return  0;
}

int main(int argc, char *argv[]) {
    static const char       short_options[] = "p:c:";
    static struct option    long_options[] = {
            { "port",               1,  NULL,   'p' },
            { "cpu",                1,  NULL,   'c' },
            { 0, 0, 0, 0 }
    };
    int                   option_index = 0;
    int                   c = 0;

    optind = 1;
    opterr = 1;
    while ((c = getopt_long(argc, argv, short_options,
                            long_options, &option_index)) != EOF) {
        switch (c) {
            case 'p':
                if (optarg) {
                    _server_port = strtol(optarg, NULL, 10);
                    if (_server_port < 1000) {
                        fprintf(stderr, "ERROR: Invalid port value!\n\n");
                        return -EINVAL;
                    }
                } else {
                    fprintf(stderr, "ERROR: Invalid port params!\n\n");
                    return -EINVAL;
                }
                break;

            case 'c':
                if (optarg) {
                    _cpu_set = strtol(optarg, NULL, 10);
                    if (_cpu_set < 0 || _cpu_set > 15) {
                        fprintf(stderr, "ERROR: Invalid cpu affinity value!\n\n");
                        return -EINVAL;
                    }
                } else {
                    fprintf(stderr, "ERROR: Invalid cpu affinity params!\n\n");
                    return -EINVAL;
                }
                break;

            default:
                fprintf(stderr, "ERROR: Invalid options! ('%c')\n\n", c);
                return -EINVAL;
        }
    }

    return _server_main();
}
