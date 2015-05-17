#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef WIN32
#include <windows.h>
HWND NvimHwnd = NULL;
HWND RConsole = NULL;
#else
#include <stdint.h>
#include <sys/socket.h>
#include <netdb.h>
#endif

static char NvimcomPort[16];
static char MyOwnPort[16];
static char OtherNvimPort[16];

#ifndef WIN32
static void SendToServer(const char *port, const char *msg)
{
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int s, a;
    size_t len;

    /* Obtain address(es) matching host/port */
    if(strncmp(port, "0", 15) == 0){
        fprintf(stderr, "Port is 0\n");
        fflush(stderr);
        return;
    }

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    a = getaddrinfo("127.0.0.1", port, &hints, &result);
    if (a != 0) {
        fprintf(stderr, "Error in getaddrinfo [port = '%s'] [msg = '%s']: %s\n", port, msg, gai_strerror(a));
        fflush(stderr);
        return;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        s = socket(rp->ai_family, rp->ai_socktype,
                rp->ai_protocol);
        if (s == -1)
            continue;

        if (connect(s, rp->ai_addr, rp->ai_addrlen) != -1)
            break;		   /* Success */

        close(s);
    }

    if (rp == NULL) {		   /* No address succeeded */
        fprintf(stderr, "Could not connect.\n");
        fflush(stderr);
        return;
    }

    freeaddrinfo(result);	   /* No longer needed */

    len = strlen(msg);
    if (write(s, msg, len) != len) {
        fprintf(stderr, "Partial/failed write.\n");
        fflush(stderr);
        return;
    }
}
#endif

#ifdef WIN32
static void SendToServer(const char *port, const char *msg)
{
    WSADATA wsaData;
    struct sockaddr_in peer_addr;
    SOCKET sfd;

    if(strncmp(port, "0", 15) == 0){
        fprintf(stderr, "Port is 0\n");
        fflush(stderr);
        return;
    }

    WSAStartup(MAKEWORD(2, 2), &wsaData);
    sfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if(sfd < 0){
        fprintf(stderr, "Socket failed\n");
        fflush(stderr);
        return;
    }

    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(atoi(port));
    peer_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if(connect(sfd, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) < 0){
        fprintf(stderr, "Could not connect\n");
        fflush(stderr);
        return;
    }

    int len = strlen(msg);
    if (send(sfd, msg, len+1, 0) < 0) {
        fprintf(stderr, "Failed sending message\n");
        fflush(stderr);
        return;
    }

    if(closesocket(sfd) < 0){
        fprintf(stderr, "Error closing socket\n");
        fflush(stderr);
    }
}

static void FindRConsole(){
    RConsole = FindWindow(NULL, "R Console (64-bit)");
    if(!RConsole){
        RConsole = FindWindow(NULL, "R Console (32-bit)");
        if(!RConsole)
            RConsole = FindWindow(NULL, "R Console");
    }
    if(!RConsole){
        fprintf(stderr, "R Console not found\n");
        fflush(stderr);
    }
}

static void RaiseNvimWindow()
{
    if(NvimHwnd){
        SetForegroundWindow(NvimHwnd);
    } else {
        fprintf(stderr, "Nvim window handle not defined\n");
        fflush(stderr);
    }
}

static void RaiseRConsole(){
    SetForegroundWindow(RConsole);
    Sleep(0.05);
}

static void SendToRConsole(char *aString){
    if(!RConsole)
        FindRConsole();
    if(!RConsole){
        fprintf(stderr, "R Console not found\n");
        fflush(stderr);
        return;
    }

    SendToServer(NvimcomPort, "\003Set R as busy [SendToRConsole()]");
    RaiseRConsole();

    const size_t len = strlen(aString) + 1;
    HGLOBAL h =  GlobalAlloc(GMEM_MOVEABLE, len);
    memcpy(GlobalLock(h), aString, len);
    GlobalUnlock(h);
    OpenClipboard(0);
    EmptyClipboard();
    SetClipboardData(CF_TEXT, h);
    CloseClipboard();

    // This is the most inefficient way of sending Ctrl+V. See:
    // http://stackoverflow.com/questions/27976500/postmessage-ctrlv-without-raising-the-window
    keybd_event(VK_CONTROL, 0, 0, 0);
    keybd_event(VkKeyScan('V'), 0, KEYEVENTF_EXTENDEDKEY | 0, 0);
    Sleep(0.05);
    keybd_event(VkKeyScan('V'), 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
}

static void RClearConsole(){
    if(!RConsole)
        FindRConsole();
    if(!RConsole){
        fprintf(stderr, "R Console not found\n");
        fflush(stderr);
        return;
    }

    RaiseRConsole();
    keybd_event(VK_CONTROL, 0, 0, 0);
    keybd_event(VkKeyScan('L'), 0, KEYEVENTF_EXTENDEDKEY | 0, 0);
    Sleep(0.05);
    keybd_event(VkKeyScan('L'), 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
}

static void SaveWinPos(char *cachedir){
    if(!RConsole)
        FindRConsole();
    if(!RConsole){
        fprintf(stderr, "R Console not found\n");
        fflush(stderr);
        return;
    }

    RECT rcR, rcV;
    if(!GetWindowRect(RConsole, &rcR)){
        fprintf(stderr, "Could not get R Console position\n");
        fflush(stderr);
        return;
    }

    if(!GetWindowRect(NvimHwnd, &rcV)){
        fprintf(stderr, "Could not get Neovim position\n");
        fflush(stderr);
        return;
    }

    rcR.right = rcR.right - rcR.left;
    rcR.bottom = rcR.bottom - rcR.top;
    rcV.right = rcV.right - rcV.left;
    rcV.bottom = rcV.bottom - rcV.top;

    char fname[512];
    snprintf(fname, 511, "%s/win_pos", cachedir);
    FILE *f = fopen(fname, "w");
    if(f == NULL){
        fprintf(stderr, "Could not write to '%s'\n", fname);
        fflush(stderr);
        return;
    }
    fprintf(f, "%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n%ld\n",
            rcR.left, rcR.top, rcR.right, rcR.bottom,
            rcV.left, rcV.top, rcV.right, rcV.bottom);
    fclose(f);
}

static void ArrangeWindows(char *cachedir){
    char fname[512];
    snprintf(fname, 511, "%s/win_pos", cachedir);
    FILE *f = fopen(fname, "r");
    if(f == NULL){
        fprintf(stderr, "Could not read '%s'\n", fname);
        fflush(stderr);
        return;
    }

    if(!RConsole)
        FindRConsole();
    if(!RConsole){
        fprintf(stderr, "R Console not found\n");
        fflush(stderr);
        return;
    }

    RECT rcR, rcV;
    char b[32];
    if((fgets(b, 31, f))){
        rcR.left = atol(b);
    } else {
        fprintf(stderr, "Error reading R left position\n");
        fflush(stderr);
        return;
    }
    if((fgets(b, 31, f))){
        rcR.top = atol(b);
    } else {
        fprintf(stderr, "Error reading R top position\n");
        fflush(stderr);
        return;
    }
    if((fgets(b, 31, f))){
        rcR.right = atol(b);
    } else {
        fprintf(stderr, "Error reading R right position\n");
        fflush(stderr);
        return;
    }
    if((fgets(b, 31, f))){
        rcR.bottom = atol(b);
    } else {
        fprintf(stderr, "Error reading R bottom position\n");
        fflush(stderr);
        return;
    }
    if((fgets(b, 31, f))){
        rcV.left = atol(b);
    } else {
        fprintf(stderr, "Error reading Neovim left position\n");
        fflush(stderr);
        return;
    }
    if((fgets(b, 31, f))){
        rcV.top = atol(b);
    } else {
        fprintf(stderr, "Error reading Neovim top position\n");
        fflush(stderr);
        return;
    }
    if((fgets(b, 31, f))){
        rcV.right = atol(b);
    } else {
        fprintf(stderr, "Error reading Neovim right position\n");
        fflush(stderr);
        return;
    }
    if((fgets(b, 31, f))){
        rcV.bottom = atol(b);
    } else {
        fprintf(stderr, "Error reading Neovim bottom position\n");
        fflush(stderr);
        return;
    }

    if(!SetWindowPos(RConsole, HWND_TOP,
                rcR.left, rcR.top, rcR.right, rcR.bottom, 0)){
        fprintf(stderr, "Error positioning Neovim window\n");
        fflush(stderr);
        return;
    }
    Sleep(0.05);
    if(!SetWindowPos(NvimHwnd, HWND_TOP,
                rcV.left, rcV.top, rcV.right, rcV.bottom, 0)){
        fprintf(stderr, "Error positioning Neovim window\n");
        fflush(stderr);
        return;
    }
}
#endif

int main(int argc, char **argv){
    char line[1024];
    char *msg;
    memset(line, 0, 1024);
    strcpy(NvimcomPort, "0");
    strcpy(OtherNvimPort, "0");
    strcpy(MyOwnPort, "0");

    if(argc == 5){
        snprintf(line, 1023, "%scall SyncTeX_backward('%s', %s)", argv[2], argv[3], argv[4]);
        SendToServer(argv[1], line);
        if(getenv("DEBUG_NVIMR")){
            FILE *df1 = fopen("/tmp/nvimrclient_1_debug", "w");
            if(df1 != NULL){
                fprintf(df1, "%s %s %s %s\n", argv[1], argv[2], argv[3], argv[4]);
                fclose(df1);
            }
            return 0;
        }
    }

    FILE *df = NULL;
    if(getenv("DEBUG_NVIMR")){
        df = fopen("nvimrclient_debug", "w");
        if(df == NULL){
            fprintf(stderr, "Error opening \"nvimrclient_debug\" for writing\n");
            fflush(stderr);
        }
    }

#ifdef WIN32
    FindRConsole();
    if(!RConsole){
        fprintf(stderr, "Could not find \"R Console\" window\n");
        fflush(stderr);
    }
    NvimHwnd = FindWindow(NULL, "Neovim");
    if(!NvimHwnd){
        fprintf(stderr, "Could not find \"Neovim\" window\n");
        fflush(stderr);
    }
#endif

    while(fgets(line, 1023, stdin)){
        if(df){
            fprintf(df, "%s", line);
            fflush(df);
        }

        for(int i = 0; i < strlen(line); i++)
            if(line[i] == '\n' || line[i] == '\r')
                line[i] = 0;
        msg = line;
        switch(*msg){
            case 1: // SetPort
                msg++;
                if(*msg == 'R'){
                    msg++;
                    strncpy(NvimcomPort, msg, 15);
#ifdef WIN32
                    if(msg[0] == '0')
                        RConsole = NULL;
#endif
                } else if(msg[0] == 'S'){
                    msg++;
                    strncpy(MyOwnPort, msg, 15);
                } else {
                    msg++;
                    strncpy(OtherNvimPort, msg, 15);
                }
                break;
            case 2: // Send message
                msg++;
                if(msg[0] == 'R'){
                    msg++;
                    SendToServer(NvimcomPort, msg);
                } else if(msg[0] == 'S'){
                    msg++;
                    SendToServer(MyOwnPort, msg);
                } else if(msg[0] == 'O'){
                    msg++;
                    SendToServer(OtherNvimPort, msg);
                } else {
                    fprintf(stderr, "Invalid message to send: \"%s\"\n", msg);
                    fflush(stderr);
                }
                break;
#ifdef WIN32
            case 3: // SendToRConsole
                msg++;
                strncat(line, "\n", 1023);
                SendToRConsole(msg);
                break;
            case 4: // SaveWinPos
                msg++;
                SaveWinPos(msg);
                break;
            case 5: // ArrangeWindows
                msg++;
                ArrangeWindows(msg);
                break;
            case 6:
                RClearConsole();
                break;
            case 7: // RaiseNvimWindow
                RaiseNvimWindow();
                break;
            case 8: // Set R Console window handle
                msg++;
#ifdef _WIN64
                RConsole = (HWND)atoll(msg);
#else
                RConsole = (HWND)atol(msg);
#endif
                break;
#endif
            default:
                fprintf(stderr, "Unknown command received: [%d] %s\n", line[0], msg);
                fflush(stderr);
                break;
        }
        memset(line, 0, 1024);
    }
    if(df)
        fclose(df);
    return 0;
}
