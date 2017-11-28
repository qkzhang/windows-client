#include <iostream>
#include <windows.h>
#include <winsock2.h>
#include <stdlib.h>
#include <getopt.h>

u_short _server_port = 2800;
char _server_ip[16] = "122.114.199.199";
int _msgsize = 1024;
int _msgcount = 1;
int _internal = 1000;
int _cpu = 0;
int _tcpnodelay = 0;
int _sndbuf = 0;
int _block = 0;
int _iovector = 0;

/*
 * 微妙延时
 */
void sleep_micro_seconds(ULONG ulMicroSeconds)
{
    if (ulMicroSeconds >= 1000) {
        Sleep(ulMicroSeconds/1000);
    } else {
        LARGE_INTEGER varFrequency = {0}, varCounter_Start = {0}, varCounter_End = {0};
        LONGLONG llCount = 0;

        ::QueryPerformanceFrequency(&varFrequency);
        llCount = ulMicroSeconds*varFrequency.QuadPart/1000000;

        ::QueryPerformanceCounter(&varCounter_Start);
        while(true)
        {
            ::QueryPerformanceCounter(&varCounter_End);
            if(llCount < varCounter_End.QuadPart - varCounter_Start.QuadPart)
            {
                break;
            }
        }
    }
}

/*
 * 发送数据
 *
 * return   > 0 发送延迟大小(ns)
 *          < 0 发送失败
 */
LONGLONG _client_send(const SOCKET clientfd, char *szMsg, int msgSize) {
    LARGE_INTEGER freq;
    LARGE_INTEGER StartingTime;
    LARGE_INTEGER EndingTime;
    WSAOVERLAPPED SendOverlapped;
    WSABUF ioBuf[2];
    DWORD SendBytes = 0;
    DWORD Flags = 0;
    int iResult = 0;
    int sendLen = 0;

    if (_iovector != 0) {
        ioBuf[0].buf = szMsg;
        ioBuf[0].len = (unsigned long)(msgSize/2);

        ioBuf[1].buf = szMsg + ioBuf[0].len;
        ioBuf[1].len = msgSize - ioBuf[0].len;
    }

    if (_iovector == 2) {
        memset(&SendOverlapped, 0, sizeof(WSAOVERLAPPED));
        SendOverlapped.hEvent = WSACreateEvent();
        if (SendOverlapped.hEvent == NULL) {
            printf("WSACreateEvent failed with error: %d\n", WSAGetLastError());
            return -1LL;
        }
    }

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&StartingTime);
    do {
        if (_iovector == 0) {
            iResult = send(clientfd, szMsg + sendLen, msgSize - sendLen, 0);
            if (iResult > 0) {
                sendLen += iResult;
            } else if (iResult == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
                //printf("send is block on nonblocking mode!\n");
                continue;
            } else {
                printf("send failed! lasterr:%d\n", WSAGetLastError());
                return -1LL;
            }
        } else if(_iovector == 1) {
            iResult = WSASend(clientfd, ioBuf, 2, &SendBytes, 0, NULL, NULL);
            if ((iResult == SOCKET_ERROR) && (WSA_IO_PENDING != WSAGetLastError())) {
                printf("WSASend failed with error: %d\n", WSAGetLastError());
                return -1LL;
            } else {
                sendLen += SendBytes;
            }
        } else if (_iovector == 2) {
            iResult = WSASend(clientfd, ioBuf, 2, &SendBytes, 0, &SendOverlapped, NULL);
            if ((iResult == SOCKET_ERROR) && (WSA_IO_PENDING !=  WSAGetLastError())) {
                printf("WSASend failed with error: %d\n", WSAGetLastError());
                return -1LL;
            }

            iResult = WSAWaitForMultipleEvents(1, &SendOverlapped.hEvent, TRUE, INFINITE, TRUE);
            if (iResult == WSA_WAIT_FAILED) {
                printf("WSAWaitForMultipleEvents failed with error: %d\n",
                       WSAGetLastError());
                break;
            }

            iResult = WSAGetOverlappedResult(clientfd, &SendOverlapped, &SendBytes,
                                             FALSE, &Flags);
            if (iResult == FALSE) {
                printf("WSASend failed with error: %d\n", WSAGetLastError());
                break;
            }
            sendLen += SendBytes;

            WSAResetEvent(SendOverlapped.hEvent);
        } else {
            return -1;
        }
    } while (sendLen < msgSize);
    QueryPerformanceCounter(&EndingTime);

    return (EndingTime.QuadPart - StartingTime.QuadPart)*1000000000/(freq.QuadPart);
}

/*
 * 客户端发送数据的主函数
 * return
 *          ==0 发送成功
 *          <0  发送失败
 */
int _client_main() {
    WSADATA wsadata;
    SOCKET clientfd = INVALID_SOCKET;
    struct sockaddr_in serveraddr;
    LONGLONG  totalInternal = 0LL;
    LONGLONG  sendInternal = 0LL;
    int iOptVal = 0;
    int iOptLen = sizeof(iOptVal);
    DWORD dwOptVal = 0;
    int dwOptLen = sizeof(dwOptVal);
    char *szMsg = NULL;
    unsigned long arg = 1;
    int iResult = 0;

    szMsg = (char *)malloc(_msgsize);
    memset(szMsg, 1, _msgsize);

    iResult = WSAStartup(MAKEWORD(2,2 ), &wsadata);
    if (iResult != 0) {
        printf("WSAStartup failed!\n");
        return -1;
    }

    clientfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientfd == INVALID_SOCKET) {
        printf("socket create failed! lasterr:%d\n", WSAGetLastError());
        WSACleanup();
        return -1;
    }

    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(_server_port);
    serveraddr.sin_addr.S_un.S_addr = inet_addr(_server_ip);
    iResult = connect(clientfd, (const struct sockaddr *)&serveraddr, sizeof(struct sockaddr));
    if (iResult == SOCKET_ERROR) {
        printf("connect failed! lasterr:%d\n", WSAGetLastError());
        closesocket(clientfd);
        WSACleanup();
        return  -1;
    }

    if (! _block) {
        iResult = ioctlsocket(clientfd, FIONBIO, &arg);
        if (iResult != NO_ERROR) {
            printf("ioctlsocket FIONBIO failed! lasterr:%d\n", WSAGetLastError());
        }
    }

    if(SOCKET_ERROR == getsockopt(clientfd, IPPROTO_TCP, TCP_NODELAY, (char *) &iOptVal, &iOptLen)) {
        printf("get TCP_NODELAY failed ! lasterr = %d\n", WSAGetLastError());
    }

    if (_tcpnodelay && iOptVal == 0) {
        iOptVal = 1;
        if(SOCKET_ERROR == setsockopt(clientfd, IPPROTO_TCP, TCP_NODELAY, (char *) &iOptVal, iOptLen)) {
            printf("setsockopt TCP_NODELAY failed ! lasterr = %d\n", WSAGetLastError());
        }

        if(SOCKET_ERROR != getsockopt(clientfd, IPPROTO_TCP, TCP_NODELAY, (char *) &iOptVal, &iOptLen)) {
            if (iOptVal != 0) {
                printf("TCP_NODELAY is set to %d !\n", iOptVal);
            }
        }
    }

    if(SOCKET_ERROR == getsockopt(clientfd, SOL_SOCKET, SO_SNDBUF, (char *) &dwOptVal, &dwOptLen)) {
        printf("get SO_SNDBUF failed ! lasterr = %d\n", WSAGetLastError());
    }

    if (_sndbuf > 0) {
        dwOptVal = (DWORD)_sndbuf;
        if(SOCKET_ERROR == setsockopt(clientfd, SOL_SOCKET, SO_SNDBUF, (char *) &dwOptVal, dwOptLen)) {
            printf("setsockopt SO_SNDBUF failed ! lasterr = %d\n", WSAGetLastError());
        }

        if(SOCKET_ERROR != getsockopt(clientfd, SOL_SOCKET, SO_SNDBUF, (char *) &dwOptVal, &dwOptLen)) {
            printf("SO_SNDBUF is set to %d !\n", dwOptVal);
        }
    }

    for (int i = 0; i < _msgcount; i++) {
        sendInternal = _client_send(clientfd, szMsg, _msgsize);
        if (sendInternal < 0) {
            printf("client send failed ! lasterr:%d\n", WSAGetLastError());
            closesocket(clientfd);
            WSACleanup();
            return -1;
        }

        totalInternal += sendInternal;
        sleep_micro_seconds(_internal);
    }

    printf("The average latency is %.02f us!\n", (double)(totalInternal*1.0/_msgcount/1000));

    closesocket(clientfd);
    WSACleanup();
    return 0;
}

int main(int argc, char * argv[]) {
    static const char       short_options[] = "i:p:m:n:d:c:t:s:b:v:";
    static struct option    long_options[] = {
            { "ipAddr",             1,  NULL,   'i' },
            { "port",               1,  NULL,   'p' },
            { "msg-size",           1,  NULL,   'm' },
            { "msg-count",          1,  NULL,   'n' },
            { "interval",           1,  NULL,   'd' },
            { "cpu",                1,  NULL,   'c' },
            { "tcp-nodelay",        1,  NULL,   't' },
            { "snd-buf",            1,  NULL,   's' },
            { "block",              1,  NULL,   'b' },
            { "iovector",           1,  NULL,   'v' },
            { 0, 0, 0, 0 }
    };

    int                   option_index = 0;
    int                   c = 0;

    optind = 1;
    opterr = 1;
    while ((c = getopt_long(argc, argv, short_options,
                            long_options, &option_index)) != EOF) {
        switch (c) {
            case 'i':
                if (optarg) {
                    strcpy(_server_ip, optarg);
                    _server_ip[sizeof(_server_ip) - 1] = '\0';
                    if (strlen(_server_ip) == 0) {
                        fprintf(stderr, "ERROR: Invalid ip address value!\n\n");
                        return -EINVAL;
                    }
                } else {
                    fprintf(stderr, "ERROR: Invalid ip address params!\n\n");
                    return -EINVAL;
                }
                break;

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

            case 'm':
                if (optarg) {
                    _msgsize = strtol(optarg, NULL, 10);
                    if (_msgsize < 1) {
                        fprintf(stderr, "ERROR: Invalid msgSize value!\n\n");
                        return -EINVAL;
                    }
                } else {
                    fprintf(stderr, "ERROR: Invalid msgSize params!\n\n");
                    return -EINVAL;
                }
                break;

            case 'n':
                if (optarg) {
                    _msgcount = strtol(optarg, NULL, 10);
                    if (_msgcount <= 0) {
                        fprintf(stderr, "ERROR: Invalid msg-count value!\n\n");
                        return -EINVAL;
                    }
                } else {
                    fprintf(stderr, "ERROR: Invalid msg-count params!\n\n");
                    return -EINVAL;
                }
                break;

            case 'd':
                if (optarg) {
                    _internal = strtol(optarg, NULL, 10);
                    if (_internal < 0 || _internal > 100000000) {
                        fprintf(stderr, "ERROR: Invalid interval value!\n\n");
                        return -EINVAL;
                    }
                } else {
                    fprintf(stderr, "ERROR: Invalid interval params!\n\n");
                    return -EINVAL;
                }
                break;

            case 'c':
                if (optarg) {
                    _cpu = strtol(optarg, NULL, 10);
                    if (_cpu < 0 || _cpu > 15) {
                        fprintf(stderr, "ERROR: Invalid cpu affinity value!\n\n");
                        return -EINVAL;
                    }
                } else {
                    fprintf(stderr, "ERROR: Invalid cpu affinity params!\n\n");
                    return -EINVAL;
                }
                break;

            case 't':
                if (optarg) {
                    _tcpnodelay = strtol(optarg, NULL, 10);
                } else {
                    fprintf(stderr, "ERROR: Invalid tcp nodelay params!\n\n");
                    return -EINVAL;
                }
                break;

            case 's':
                if (optarg) {
                    _sndbuf = strtol(optarg, NULL, 10);
                    if (_sndbuf <= 0) {
                        fprintf(stderr, "ERROR: Invalid snd-buf value!\n\n");
                        return -EINVAL;
                    }
                } else {
                    fprintf(stderr, "ERROR: Invalid snd-buf params!\n\n");
                    return -EINVAL;
                }
                break;

            case 'b':
                if (optarg) {
                    _block = strtol(optarg, NULL, 10);
                } else {
                    fprintf(stderr, "ERROR: Invalid block params!\n\n");
                    return -EINVAL;
                }
                break;

            case 'v':
                if (optarg) {
                    _iovector = strtol(optarg, NULL, 10);
                } else {
                    fprintf(stderr, "ERROR: Invalid iovector params!\n\n");
                    return -EINVAL;
                }
                break;

            default:
                fprintf(stderr, "ERROR: Invalid options! ('%c')\n\n", c);
                return -EINVAL;
        }
    }

    return _client_main();
}