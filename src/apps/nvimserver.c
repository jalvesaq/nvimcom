#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>

#ifdef WIN32
#include <winsock2.h>
#else
#include <stdint.h>
#include <sys/socket.h>
#include <netdb.h>
#include <signal.h>
#endif

static char VimSecret[128];
static int VimSecretLen;


FILE *RegisterPort(int bindportn)
{
    FILE *df = NULL;
    // Register the port:
    printf("call RSetMyPort('%d')\n", bindportn);
    fflush(stdout);

    if(getenv("DEBUG_NVIMR")){
        char fn[512];
        snprintf(fn, 511, "nvimserver_debug_%d", bindportn);
        df = fopen(fn, "w");
        if(df == NULL)
            fprintf(stderr, "Error opening %s for writing\n", fn);
    }
    return(df);
}

static void ParseMsg(char *buf, FILE *df)
{
    char *bbuf = buf;
    if(df){
        fprintf(df, "%s\n", bbuf);
        fflush(df);
    }

    if(strstr(bbuf, VimSecret)){
        bbuf += VimSecretLen;
        printf("%s\n", bbuf);
        fflush(stdout);
    } else {
        fprintf(stderr, "Strange string received: \"%s\"\n", bbuf);
        fflush(stderr);
    }
}

#ifndef WIN32
static void NeovimServer()
{
    unsigned short bindportn = 10100;
    ssize_t nread;
    int bsize = 5012;
    char buf[bsize];
    int result;
    FILE *df;

    struct addrinfo hints;
    struct addrinfo *rp;
    struct addrinfo *res;
    struct sockaddr_storage peer_addr;
    int sfd = -1;
    char bindport[16];
    socklen_t peer_addr_len = sizeof(struct sockaddr_storage);

    // block SIGINT
    {
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGINT);
        sigprocmask(SIG_BLOCK, &set, NULL);
    }

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
    hints.ai_flags = AI_PASSIVE;    /* For wildcard IP address */
    hints.ai_protocol = 0;          /* Any protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;
    rp = NULL;
    result = 1;
    while(rp == NULL && bindportn < 10149){
        bindportn++;
        sprintf(bindport, "%d", bindportn);
        result = getaddrinfo("127.0.0.1", bindport, &hints, &res);
        if(result != 0){
            fprintf(stderr, "Neovim server: Error at getaddrinfo (%s)\n", gai_strerror(result));
            fflush(stderr);
            return;
        }

        for (rp = res; rp != NULL; rp = rp->ai_next) {
            sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (sfd == -1)
                continue;
            if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
                break;       /* Success */
            close(sfd);
        }
        freeaddrinfo(res);   /* No longer needed */
    }

    if (rp == NULL) {        /* No address succeeded */
        fprintf(stderr, "Neovim server: Could not bind.\n");
        fflush(stderr);
        return;
    }

    df = RegisterPort(bindportn);

    /* Read datagrams and reply to sender */
    for (;;) {
        memset(buf, 0, bsize);

        nread = recvfrom(sfd, buf, bsize, 0,
                (struct sockaddr *) &peer_addr, &peer_addr_len);
        if (nread == -1){
            fprintf(stderr, "Neovim server [port %d]: recvfrom failed\n", bindportn);
            fflush(stderr);
            continue;     /* Ignore failed request */
        }

        ParseMsg(buf, df);
    }
    if(df)
        fclose(df);
    return;
}
#endif

#ifdef WIN32
static void NeovimServer()
{
    unsigned short bindportn = 10100;
    ssize_t nread;
    int bsize = 5012;
    char buf[bsize];
    int result;
    FILE *df;

    WSADATA wsaData;
    SOCKADDR_IN RecvAddr;
    SOCKADDR_IN peer_addr;
    SOCKET sfd;
    int peer_addr_len = sizeof (peer_addr);
    int nattp = 0;
    int nfail = 0;
    //int lastfail = 0;

    result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != NO_ERROR) {
        fprintf(stderr, "Neovim server: WSAStartup failed with error %d.\n", result);
        fflush(stderr);
        return;
    }

    while(bindportn < 10149){
        bindportn++;
        sfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sfd == INVALID_SOCKET) {
            fprintf(stderr, "Neovim server: socket failed with error %d\n", WSAGetLastError());
            fflush(stderr);
            return;
        }

        RecvAddr.sin_family = AF_INET;
        RecvAddr.sin_port = htons(bindportn);
        RecvAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        nattp++;
        if(bind(sfd, (SOCKADDR *) & RecvAddr, sizeof (RecvAddr)) == 0)
            break;
        //lastfail = WSAGetLastError();
        nfail++;
        // fprintf(stderr, "Neovim server: Could not bind to port %d [error  %d].\n", bindportn, lastfail);
        // fflush(stderr);
    }
    if(nattp == nfail){
        fprintf(stderr, "Neovim server: Could not bind.\n");
        fflush(stderr);
        return;
    }

    df = RegisterPort(bindportn);

    /* Read datagrams and reply to sender */
    for (;;) {
        memset(buf, 0, bsize);

        nread = recvfrom(sfd, buf, bsize, 0,
                (SOCKADDR *) &peer_addr, &peer_addr_len);
        if (nread == SOCKET_ERROR) {
            fprintf(stderr, "Neovim server [port %d]: recvfrom failed with error %d\n",
                    bindportn, WSAGetLastError());
            fflush(stderr);
            return;
        }

        ParseMsg(buf, df);
    }
    fprintf(stderr, "Neovim server: Finished receiving. Closing socket.\n");
    fflush(stderr);
    result = closesocket(sfd);
    if (result == SOCKET_ERROR) {
        fprintf(stderr, "Neovim server: closesocket failed with error %d\n", WSAGetLastError());
        fflush(stderr);
        return;
    }
    WSACleanup();
    if(df)
        fclose(df);
    return;
}
#endif

int main(int argc, char **argv){
    if(!getenv("NVIMR_SECRET")){
        fprintf(stderr, "NVIMR_SECRET not found\n");
        fflush(stderr);
        exit(1);
    }
    strncpy(VimSecret, getenv("NVIMR_SECRET"), 127);
    VimSecretLen = strlen(VimSecret);
#ifdef WIN32
    Sleep(1000);
#else
    sleep(1);
#endif
    NeovimServer();
    return 0;
}
