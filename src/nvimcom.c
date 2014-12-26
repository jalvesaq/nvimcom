
#include <R.h>  /* to include Rconfig.h */
#include <Rinternals.h>
#include <R_ext/Parse.h>
#include <R_ext/Callbacks.h>
#ifndef WIN32
#include <R_ext/eventloop.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>

#ifdef WIN32
#include <windows.h>
#include <process.h>
#ifdef _WIN64
#include <inttypes.h>
#endif
#else
#include <stdint.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#endif

static char nvimcom_version[32];

static int Neovim = 0;
static pid_t R_PID;

static int nvimcom_initialized = 0;
static int verbose = 0;
static int opendf = 1;
static int openls = 0;
static int allnames = 0;
static int labelerr = 1;
static int nvimcom_is_utf8;
static int nvimcom_failure = 0;
static int nlibs = 0;
static int openclosel = 0;
static int nobjs = 0;
static char obsrvr[128];
static char edsrvr[128];
static char nvimsecr[128];
static char liblist[512];
static char globenv[512];
static char strL[16];
static char strT[16];
static char tmpdir[512];
static char nvimcom_home[1024];
static int objbr_auto = 0;
static int has_new_lib = 0;
static int has_new_obj = 0;
static int always_ls_env = 0;

#ifdef WIN32
static int r_is_busy = 1;
static int tcltkerr = 0;
static int toggling_list = 0;
#else
static int fired = 0;
static char flag_eval[512];
static int flag_lsenv = 0;
static int flag_lslibs = 0;
static int ifd, ofd;
static InputHandler *ih;
#endif

typedef struct liststatus_ {
    char *key;
    int status;
    struct liststatus_ *next;
} ListStatus;

static int nvimcom_checklibs();

static ListStatus *firstList = NULL;

static char *loadedlibs[64];
static char *builtlibs[64];

#ifdef WIN32
SOCKET sfd;
static int tid;
#else
static int sfd = -1;
static pthread_t tid;
#endif

static void nvimcom_del_newline(char *buf)
{
    for(int i = 0; i < strlen(buf); i++)
        if(buf[i] == '\n'){
            buf[i] = 0;
            break;
        }
}

#ifndef WIN32
static void nvimcom_nvimclient(const char *msg, char *port)
{
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    char portstr[16];
    int s, a;
    size_t len;
    int srvport = atoi(port);

    if(verbose > 2)
        Rprintf("nvimcom_nvimclient(%s): '%s' (%d)\n", msg, port, srvport);
    if(port[0] == 0){
        if(verbose > 3)
            REprintf("nvimcom_nvimclient() called although Neovim server port is undefined\n");
        return;
    }

    /* Obtain address(es) matching host/port */

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    sprintf(portstr, "%d", srvport);
    a = getaddrinfo("127.0.0.1", portstr, &hints, &result);
    if (a != 0) {
        REprintf("Error: getaddrinfo: %s\n", gai_strerror(a));
        objbr_auto = 0;
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
        REprintf("Error: Could not connect\n");
        objbr_auto = 0;
        return;
    }

    freeaddrinfo(result);	   /* No longer needed */

    /* Prefix NVIMR_SECRET to msg to increase security.
     * The nvimclient does not need this because it is protect by the X server. */
    char finalmsg[256];
    strncpy(finalmsg, nvimsecr, 255);
    strncat(finalmsg, "call ", 255);
    strncat(finalmsg, msg, 255);
    len = strlen(finalmsg);
    if (write(s, finalmsg, len) != len) {
        REprintf("Error: partial/failed write\n");
        objbr_auto = 0;
        return;
    }
}
#endif

#ifdef WIN32
static void nvimcom_nvimclient(const char *msg, char *port)
{
    WSADATA wsaData;
    struct sockaddr_in peer_addr;
    SOCKET sfd;

    if(verbose > 2)
        Rprintf("nvimcom_nvimclient(%s): '%s'\n", msg, port);
    if(port[0] == 0){
        if(verbose > 3)
            REprintf("nvimcom_nvimclient() called although Neovim server port is undefined\n");
        return;
    }

    WSAStartup(MAKEWORD(2, 2), &wsaData);
    sfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if(sfd < 0){
        REprintf("nvimcom_nvimclient socket failed.\n");
        return;
    }

    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(atoi(port));
    peer_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if(connect(sfd, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) < 0){
        REprintf("nvimcom_nvimclient could not connect.\n");
        return;
    }

    /* Prefix NVIMR_SECRET to msg to increase security.
     * The nvimclient does not need this because it is protect by the X server. */
    char finalmsg[256];
    strncpy(finalmsg, nvimsecr, 255);
    strncat(finalmsg, "call ", 255);
    strncat(finalmsg, msg, 255);
    int len = strlen(finalmsg);
    if (send(sfd, finalmsg, len+1, 0) < 0) {
        REprintf("nvimcom_nvimclient failed sending message.\n");
        return;
    }

    if(closesocket(sfd) < 0)
        REprintf("nvimcom_nvimclient error closing socket.\n");
    return;
}
#endif

void nvimcom_msg_to_nvim(char **cmd)
{
    nvimcom_nvimclient(*cmd, edsrvr);
}

static void nvimcom_toggle_list_status(const char *x)
{
    ListStatus *tmp = firstList;
    while(tmp){
        if(strcmp(tmp->key, x) == 0){
            tmp->status = !tmp->status;
            break;
        }
        tmp = tmp->next;
    }
}

static void nvimcom_add_list(const char *x, int s)
{
    ListStatus *tmp = firstList;
    while(tmp->next)
        tmp = tmp->next;
    tmp->next = (ListStatus*)calloc(1, sizeof(ListStatus));
    tmp->next->key = (char*)malloc((strlen(x) + 1) * sizeof(char));
    strcpy(tmp->next->key, x);
    tmp->next->status = s;
}

static int nvimcom_get_list_status(const char *x, const char *xclass)
{
    ListStatus *tmp = firstList;
    while(tmp){
        if(strcmp(tmp->key, x) == 0)
            return(tmp->status);
        tmp = tmp->next;
    }
    if(strcmp(xclass, "data.frame") == 0){
        nvimcom_add_list(x, opendf);
        return(opendf);
    } else if(strcmp(xclass, "list") == 0){
        nvimcom_add_list(x, openls);
        return(openls);
    } else {
        nvimcom_add_list(x, 0);
        return(0);
    }
}

static void nvimcom_count_elements(SEXP *x)
{
    SEXP elmt;
    int len = length(*x);
    for(int i = 0; i < len; i++){
        nobjs++;
        elmt = VECTOR_ELT(*x, i);
        if(Rf_isNewList(elmt))
            nvimcom_count_elements(&elmt);
    }
}

static int nvimcom_count_objects()
{
    const char *varName;
    SEXP envVarsSEXP;
    SEXP varSEXP;

    int oldcount = nobjs;
    nobjs = 0;

    PROTECT(envVarsSEXP = R_lsInternal(R_GlobalEnv, 0));
    for(int i = 0; i < Rf_length(envVarsSEXP); i++){
        varName = CHAR(STRING_ELT(envVarsSEXP, i));
        PROTECT(varSEXP = Rf_findVar(Rf_install(varName), R_GlobalEnv));
        if (varSEXP != R_UnboundValue) // should never be unbound
        {
            nobjs++;
            if(Rf_isNewList(varSEXP))
                nvimcom_count_elements(&varSEXP);
        } else {
            REprintf("Unexpected R_UnboundValue returned from R_lsInternal\n");
        }
        UNPROTECT(1);
    }
    UNPROTECT(1);

    return(oldcount != nobjs);
}

static void nvimcom_browser_line(SEXP *x, const char *xname, const char *curenv, const char *prefix, FILE *f)
{
    char xclass[64];
    char newenv[512];
    char curenvB[512];
    char ebuf[64];
    char pre[128];
    char newpre[128];
    int len;
    const char *ename;
    SEXP listNames, label, lablab, eexp, elmt = R_NilValue;
    SEXP cmdSexp, cmdexpr, ans, cmdSexp2, cmdexpr2;
    ParseStatus status, status2;
    int er = 0;
    char buf[128];


    if(Rf_isLogical(*x)){
        strcpy(xclass, "logical");
        fprintf(f, "%s%%#", prefix);
    } else if(Rf_isNumeric(*x)){
        strcpy(xclass, "numeric");
        fprintf(f, "%s{#", prefix);
    } else if(Rf_isFactor(*x)){
        strcpy(xclass, "factor");
        fprintf(f, "%s'#", prefix);
    } else if(Rf_isValidString(*x)){
        strcpy(xclass, "character");
        fprintf(f, "%s\"#", prefix);
    } else if(Rf_isFunction(*x)){
        strcpy(xclass, "function");
        fprintf(f, "%s(#", prefix);
    } else if(Rf_isFrame(*x)){
        strcpy(xclass, "data.frame");
        fprintf(f, "%s[#", prefix);
    } else if(Rf_isNewList(*x)){
        strcpy(xclass, "list");
        fprintf(f, "%s[#", prefix);
    } else if(Rf_isS4(*x)){
        strcpy(xclass, "s4");
        fprintf(f, "%s<#", prefix);
    } else {
        strcpy(xclass, "other");
        fprintf(f, "%s=#", prefix);
    }

    PROTECT(lablab = allocVector(STRSXP, 1));
    SET_STRING_ELT(lablab, 0, mkChar("label"));
    PROTECT(label = getAttrib(*x, lablab));
    if(length(label) > 0){
        if(Rf_isValidString(label)){
            fprintf(f, "%s\t%s\n", xname, CHAR(STRING_ELT(label, 0)));
        } else {
            if(labelerr)
                fprintf(f, "%s\tError: label isn't \"character\".\n", xname);
            else
                fprintf(f, "%s\t\n", xname);
        }
    } else {
        fprintf(f, "%s\t\n", xname);
    }
    UNPROTECT(2);

    if(strcmp(xclass, "list") == 0 || strcmp(xclass, "data.frame") == 0 || strcmp(xclass, "s4") == 0){
        strncpy(curenvB, curenv, 500);
        if(xname[0] == '[' && xname[1] == '['){
            curenvB[strlen(curenvB) - 1] = 0;
        }
        if(strcmp(xclass, "s4") == 0)
            snprintf(newenv, 500, "%s%s@", curenvB, xname);
        else
            snprintf(newenv, 500, "%s%s$", curenvB, xname);
        if((nvimcom_get_list_status(newenv, xclass) == 1)){
            len = strlen(prefix);
            if(nvimcom_is_utf8){
                int j = 0, i = 0;
                while(i < len){
                    if(prefix[i] == '\xe2'){
                        i += 3;
                        if(prefix[i-1] == '\x80' || prefix[i-1] == '\x94'){
                            pre[j] = ' '; j++;
                        } else {
                            pre[j] = '\xe2'; j++;
                            pre[j] = '\x94'; j++;
                            pre[j] = '\x82'; j++;
                        }
                    } else {
                        pre[j] = prefix[i];
                        i++, j++;
                    }
                }
                pre[j] = 0;
            } else {
                for(int i = 0; i < len; i++){
                    if(prefix[i] == '-' || prefix[i] == '`')
                        pre[i] = ' ';
                    else
                        pre[i] = prefix[i];
                }
                pre[len] = 0;
            }
            sprintf(newpre, "%s%s", pre, strT);

            if(strcmp(xclass, "s4") == 0){
                snprintf(buf, 127, "slotNames(%s%s)", curenvB, xname);
                PROTECT(cmdSexp = allocVector(STRSXP, 1));
                SET_STRING_ELT(cmdSexp, 0, mkChar(buf));
                PROTECT(cmdexpr = R_ParseVector(cmdSexp, -1, &status, R_NilValue));

                if (status != PARSE_OK) {
                    fprintf(f, "nvimcom error: invalid value in slotNames(%s)\n", xname);
                } else {
                    PROTECT(ans = R_tryEval(VECTOR_ELT(cmdexpr, 0), R_GlobalEnv, &er));
                    if(er){
                        fprintf(f, "nvimcom error: %s\n", xname);
                    } else {
                        len = length(ans);
                        if(len > 0){
                            int len1 = len - 1;
                            for(int i = 0; i < len; i++){
                                ename = CHAR(STRING_ELT(ans, i));
                                snprintf(buf, 127, "%s%s@%s", curenvB, xname, ename);
                                PROTECT(cmdSexp2 = allocVector(STRSXP, 1));
                                SET_STRING_ELT(cmdSexp2, 0, mkChar(buf));
                                PROTECT(cmdexpr2 = R_ParseVector(cmdSexp2, -1, &status2, R_NilValue));
                                if (status2 != PARSE_OK) {
                                    fprintf(f, "nvimcom error: invalid code \"%s@%s\"\n", xname, ename);
                                } else {
                                    PROTECT(elmt = R_tryEval(VECTOR_ELT(cmdexpr2, 0), R_GlobalEnv, &er));
                                    if(i == len1)
                                        sprintf(newpre, "%s%s", pre, strL);
                                    nvimcom_browser_line(&elmt, ename, newenv, newpre, f);
                                    UNPROTECT(1);
                                }
                                UNPROTECT(2);
                            }
                        }
                    }
                    UNPROTECT(1);
                }
                UNPROTECT(2);
            } else {
                PROTECT(listNames = getAttrib(*x, R_NamesSymbol));
                len = length(listNames);
                if(len == 0){ /* Empty list? */
                    int len1 = length(*x);
                    if(len1 > 0){ /* List without names */
                        len1 -= 1;
                        for(int i = 0; i < len1; i++){
                            sprintf(ebuf, "[[%d]]", i + 1);
                            elmt = VECTOR_ELT(*x, i);
                            nvimcom_browser_line(&elmt, ebuf, newenv, newpre, f);
                        }
                        sprintf(newpre, "%s%s", pre, strL);
                        sprintf(ebuf, "[[%d]]", len1 + 1);
                        PROTECT(elmt = VECTOR_ELT(*x, len));
                        nvimcom_browser_line(&elmt, ebuf, newenv, newpre, f);
                        UNPROTECT(1);
                    }
                } else { /* Named list */
                    len -= 1;
                    for(int i = 0; i < len; i++){
                        PROTECT(eexp = STRING_ELT(listNames, i));
                        ename = CHAR(eexp);
                        UNPROTECT(1);
                        if(ename[0] == 0){
                            sprintf(ebuf, "[[%d]]", i + 1);
                            ename = ebuf;
                        }
                        PROTECT(elmt = VECTOR_ELT(*x, i));
                        nvimcom_browser_line(&elmt, ename, newenv, newpre, f);
                        UNPROTECT(1);
                    }
                    sprintf(newpre, "%s%s", pre, strL);
                    ename = CHAR(STRING_ELT(listNames, len));
                    if(ename[0] == 0){
                        sprintf(ebuf, "[[%d]]", len + 1);
                        ename = ebuf;
                    }
                    PROTECT(elmt = VECTOR_ELT(*x, len));
                    nvimcom_browser_line(&elmt, ename, newenv, newpre, f);
                    UNPROTECT(1);
                }
                UNPROTECT(1); /* listNames */
            }
        }
    }
}

static void nvimcom_list_env()
{
    const char *varName;
    SEXP envVarsSEXP, varSEXP;

    if(always_ls_env == 0 && nvimcom_count_objects() == 0)
        return;
    if(verbose > 1 && objbr_auto && !always_ls_env)
        Rprintf("Current number of Objects: %d\n", nobjs);

    if(tmpdir[0] == 0)
        return;

    FILE *f = fopen(globenv, "w");
    if(f == NULL){
        REprintf("Error: Could not write to '%s'. [nvimcom]\n", globenv);
        return;
    }

    fprintf(f, ".GlobalEnv | Libraries\n\n");

    PROTECT(envVarsSEXP = R_lsInternal(R_GlobalEnv, allnames));
    for(int i = 0; i < Rf_length(envVarsSEXP); i++){
        varName = CHAR(STRING_ELT(envVarsSEXP, i));
        PROTECT(varSEXP = Rf_findVar(Rf_install(varName), R_GlobalEnv));
        if (varSEXP != R_UnboundValue) // should never be unbound
        {
            nvimcom_browser_line(&varSEXP, varName, "", "   ", f);
        } else {
            REprintf("Unexpected R_UnboundValue returned from R_lsInternal.\n");
        }
        UNPROTECT(1);
    }
    UNPROTECT(1);
    fclose(f);
    has_new_obj = 1;
}

static void nvimcom_eval_expr(const char *buf)
{
    char fn[512];
    snprintf(fn, 510, "%s/eval_reply", tmpdir);

    if(verbose > 3)
        Rprintf("nvimcom_eval_expr: '%s'\n", buf);

    FILE *rep = fopen(fn, "w");
    if(rep == NULL){
        REprintf("Error: Could not write to '%s'. [nvimcom]\n", fn);
        return;
    }

#ifdef WIN32
    if(tcltkerr){
        fprintf(rep, "Error: \"nvimcom\" and \"tcltk\" packages are incompatible!\n");
        return;
    } else {
        if(objbr_auto == 0)
            nvimcom_checklibs();
        if(tcltkerr){
            fprintf(rep, "Error: \"nvimcom\" and \"tcltk\" packages are incompatible!\n");
            return;
        }
    }
#endif

    SEXP cmdSexp, cmdexpr, ans;
    ParseStatus status;
    int er = 0;

    PROTECT(cmdSexp = allocVector(STRSXP, 1));
    SET_STRING_ELT(cmdSexp, 0, mkChar(buf));
    PROTECT(cmdexpr = R_ParseVector(cmdSexp, -1, &status, R_NilValue));

    if (status != PARSE_OK) {
        fprintf(rep, "INVALID\n");
    } else {
        /* Only the first command will be executed if the expression includes
         * a semicolon. */
        PROTECT(ans = R_tryEval(VECTOR_ELT(cmdexpr, 0), R_GlobalEnv, &er));
        if(er){
            fprintf(rep, "ERROR\n");
        } else {
            switch(TYPEOF(ans)) {
                case REALSXP:
                    fprintf(rep, "%f\n", REAL(ans)[0]);
                    break;
                case LGLSXP:
                case INTSXP:
                    fprintf(rep, "%d\n", INTEGER(ans)[0]);
                    break;
                case STRSXP:
                    if(length(ans) > 0)
                        fprintf(rep, "%s\n", CHAR(STRING_ELT(ans, 0)));
                    else
                        fprintf(rep, "EMPTY\n");
                    break;
                default:
                    fprintf(rep, "RTYPE\n");
            }
        }
        UNPROTECT(1);
    }
    UNPROTECT(2);
    fclose(rep);
}

static int nvimcom_checklibs()
{
    const char *libname;
    char buf[256];
    char *libn;
    SEXP a, l;

    PROTECT(a = eval(lang1(install("search")), R_GlobalEnv));

    int newnlibs = Rf_length(a);
    if(nlibs == newnlibs)
        return(nlibs);

    int k = 0;
    for(int i = 0; i < newnlibs; i++){
        if(i == 62)
            break;
        PROTECT(l = STRING_ELT(a, i));
        libname = CHAR(l);
        libn = strstr(libname, "package:");
        if(libn != NULL){
            strncpy(loadedlibs[k], libname, 63);
            loadedlibs[k+1][0] = 0;
#ifdef WIN32
            if(tcltkerr == 0){
                if(strstr(libn, "tcltk") != NULL){
                    REprintf("Error: \"nvimcom\" and \"tcltk\" packages are incompatible!\n");
                    tcltkerr = 1;
                }
            }
#endif
            k++;
        }
        UNPROTECT(1);
    }
    UNPROTECT(1);
    for(int i = 0; i < 64; i++){
        if(loadedlibs[i][0] == 0)
            break;
        for(int j = 0; j < 64; j++){
            libn = strstr(loadedlibs[i], ":");
            libn++;
            if(strcmp(builtlibs[j], libn) == 0)
                break;
            if(builtlibs[j][0] == 0){
                strcpy(builtlibs[j], libn);
                sprintf(buf, "nvimcom:::nvim.buildomnils('%s')", libn);
                nvimcom_eval_expr(buf);
                break;
            }
        }
    }

    char fn[512];
    snprintf(fn, 510, "%s/libnames_%s", tmpdir, getenv("NVIMR_ID"));
    FILE *f = fopen(fn, "w");
    if(f == NULL){
        REprintf("Error: Could not write to '%s'. [nvimcom]\n", fn);
        return(newnlibs);
    }
    for(int i = 0; i < 64; i++){
        if(builtlibs[i][0] == 0)
            break;
        fprintf(f, "%s\n", builtlibs[i]);
    }
    fclose(f);
    if(edsrvr[0] != 0)
        nvimcom_nvimclient("FillRLibList()", edsrvr);

    return(newnlibs);
}

static void nvimcom_list_libs()
{
    int newnlibs;
    int len, len1;
    char *libn;
    char prefixT[64];
    char prefixL[64];
    char libasenv[64];
    SEXP x, oblist, obj;

    if(tmpdir[0] == 0)
        return;

    newnlibs = nvimcom_checklibs();

    if(newnlibs == nlibs && openclosel == 0)
        return;

    nlibs = newnlibs;
    openclosel = 0;

    FILE *f = fopen(liblist, "w");
    if(f == NULL){
        REprintf("Error: Could not write to '%s'. [nvimcom]\n", liblist);
        return;
    }
    fprintf(f, "Libraries | .GlobalEnv\n\n");

    strcpy(prefixT, "   ");
    strcpy(prefixL, "   ");
    strcat(prefixT, strT);
    strcat(prefixL, strL);

    int save_opendf = opendf;
    int save_openls = openls;
    opendf = 0;
    openls = 0;
    int i = 0;
    while(loadedlibs[i][0] != 0){
        libn = loadedlibs[i] + 8;
        fprintf(f, "   ##%s\t\n", libn);
        if(nvimcom_get_list_status(loadedlibs[i], "library") == 1){
#ifdef WIN32
            if(tcltkerr){
                REprintf("Error: Cannot open libraries due to conflict between \"nvimcom\" and \"tcltk\" packages.\n");
                i++;
                continue;
            }
#endif
            PROTECT(x = allocVector(STRSXP, 1));
            SET_STRING_ELT(x, 0, mkChar(loadedlibs[i]));
            PROTECT(oblist = eval(lang2(install("objects"), x), R_GlobalEnv));
            len = Rf_length(oblist);
            len1 = len - 1;
            for(int j = 0; j < len; j++){
                PROTECT(obj = eval(lang3(install("get"), ScalarString(STRING_ELT(oblist, j)), x), R_GlobalEnv));
                snprintf(libasenv, 63, "%s-", loadedlibs[i]);
                if(j == len1)
                    nvimcom_browser_line(&obj, CHAR(STRING_ELT(oblist, j)), libasenv, prefixL, f);
                else
                    nvimcom_browser_line(&obj, CHAR(STRING_ELT(oblist, j)), libasenv, prefixT, f);
                UNPROTECT(1);
            }
            UNPROTECT(2);
        }
        i++;
    }
    fclose(f);
    opendf = save_opendf;
    openls = save_openls;
    has_new_lib = 2;
}

Rboolean nvimcom_task(SEXP expr, SEXP value, Rboolean succeeded,
        Rboolean visible, void *userData)
{
    nvimcom_list_libs();
    if(objbr_auto){
        nvimcom_list_env();
        switch(has_new_lib + has_new_obj){
            case 1:
                if(obsrvr[0] != 0)
                    nvimcom_nvimclient("UpdateOB('GlobalEnv')", obsrvr);
                break;
            case 2:
                if(obsrvr[0] != 0)
                    nvimcom_nvimclient("UpdateOB('libraries')", obsrvr);
                break;
            case 3:
                if(obsrvr[0] != 0)
                    nvimcom_nvimclient("UpdateOB('both')", obsrvr);
                break;
        }
        has_new_lib = 0;
        has_new_obj = 0;
    }
#ifdef WIN32
    r_is_busy = 0;
#endif
    return(TRUE);
}

#ifndef WIN32
static void nvimcom_exec(){
    if(*flag_eval){
        nvimcom_eval_expr(flag_eval);
        *flag_eval = 0;
    }
    if(flag_lsenv)
        nvimcom_list_env();
    if(flag_lslibs)
        nvimcom_list_libs();
    switch(has_new_lib + has_new_obj){
        case 1:
            if(obsrvr[0] != 0)
                nvimcom_nvimclient("UpdateOB('GlobalEnv')", obsrvr);
            break;
        case 2:
            if(obsrvr[0] != 0)
                nvimcom_nvimclient("UpdateOB('libraries')", obsrvr);
            break;
        case 3:
            if(obsrvr[0] != 0)
                nvimcom_nvimclient("UpdateOB('both')", obsrvr);
            break;
    }
    has_new_lib = 0;
    has_new_obj = 0;
    flag_lsenv = 0;
    flag_lslibs = 0;
}

/* Code adapted from CarbonEL.
 * Thanks to Simon Urbanek for the suggestion on r-devel mailing list. */
static void nvimcom_uih(void *data) {
    char buf[16];
    if(read(ifd, buf, 1) < 1)
        REprintf("nvimcom error: read < 1\n");
    R_ToplevelExec(nvimcom_exec, NULL);
    fired = 0;
}

static void nvimcom_fire()
{
    if(fired)
        return;
    fired = 1;
    char buf[16];
    *buf = 0;
    if(write(ofd, buf, 1) <= 0)
        REprintf("nvimcom error: write <= 0\n");
}
#endif

#ifdef WIN32
static void nvimcom_server_thread(void *arg)
#else
static void *nvimcom_server_thread(void *arg)
#endif
{
    unsigned short bindportn = 10000;
    ssize_t nsent;
    ssize_t nread;
    int bsize = 5012;
    char buf[bsize];
    char rep[bsize];
    int result;

#ifdef WIN32
    WSADATA wsaData;
    SOCKADDR_IN RecvAddr;
    SOCKADDR_IN peer_addr;
    int peer_addr_len = sizeof (peer_addr);
    int nattp = 0;
    int nfail = 0;
    int lastfail = 0;

    result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != NO_ERROR) {
        REprintf("WSAStartup failed with error %d\n", result);
        return;
    }

    while(bindportn < 10049){
        bindportn++;
        sfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sfd == INVALID_SOCKET) {
            REprintf("Error: socket failed with error %d [nvimcom]\n", WSAGetLastError());
            return;
        }

        RecvAddr.sin_family = AF_INET;
        RecvAddr.sin_port = htons(bindportn);
        RecvAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        nattp++;
        if(bind(sfd, (SOCKADDR *) & RecvAddr, sizeof (RecvAddr)) == 0)
            break;
        lastfail = WSAGetLastError();
        nfail++;
        if(verbose > 1)
            REprintf("nvimcom: Could not bind to port %d [error  %d].\n", bindportn, lastfail);
    }
    if(nfail > 0 && verbose > 0){
        if(nfail == 1)
            REprintf("nvimcom: bind failed once with error %d.\n", lastfail);
        else
            REprintf("nvimcom: bind failed %d times and the last error was %d.\n", nfail, lastfail);
        if(nattp > nfail)
            REprintf("nvimcom: finally, bind to port %d was successful.\n", bindportn);
    }
    if(nattp == nfail){
        REprintf("Error: Could not bind. [nvimcom]\n");
        nvimcom_failure = 1;
        return;
    }
#else
    struct addrinfo hints;
    struct addrinfo *rp;
    struct addrinfo *res;
    struct sockaddr_storage peer_addr;
    char bindport[16];
    socklen_t peer_addr_len = sizeof(struct sockaddr_storage);

#ifndef __APPLE__
    // block SIGINT
    {
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGINT);
        sigprocmask(SIG_BLOCK, &set, NULL);
    }
#endif

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
    while(rp == NULL && bindportn < 10049){
        bindportn++;
        sprintf(bindport, "%d", bindportn);
        result = getaddrinfo("127.0.0.1", bindport, &hints, &res);
        if(result != 0){
            REprintf("Error at getaddrinfo: %s [nvimcom]\n", gai_strerror(result));
            nvimcom_failure = 1;
            return(NULL);
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
        REprintf("Error: Could not bind. [nvimcom]\n");
        nvimcom_failure = 1;
        return(NULL);
    }
#endif

    if(verbose > 1)
        REprintf("nvimcom port: %d\n", bindportn);

#ifndef WIN32
    if(Neovim){
        flag_lslibs = 1;
        nvimcom_fire();
    }
#endif

    // Save a file to indicate that nvimcom is running
    char fn[512];
    snprintf(fn, 510, "%s/nvimcom_running_%s", tmpdir, getenv("NVIMR_ID"));
    FILE *f = fopen(fn, "w");
    if(f == NULL){
        REprintf("Error: Could not write to '%s'. [nvimcom]\n", fn);
    } else {
#ifdef _WIN64
        fprintf(f, "%s\n%s\n%d\n%" PRId64 "\n", nvimcom_version, nvimcom_home, bindportn, R_PID);
#else
        fprintf(f, "%s\n%s\n%d\n%d\n", nvimcom_version, nvimcom_home, bindportn, R_PID);
#endif
#ifdef WIN32
        HWND myHandle = GetForegroundWindow();
        snprintf(fn, 510, "%s/rconsole_hwnd_%s", tmpdir, getenv("NVIMR_SECRET"));
        FILE *h = fopen(fn, "w");
        if(h){
            fwrite(&myHandle, sizeof(HWND), 1, h);
            fclose(h);
        }
#endif
        fclose(f);
    }

    /* Read datagrams and reply to sender */
    for (;;) {
        memset(buf, 0, bsize);
        memset(rep, 0, bsize);
        strcpy(rep, "UNKNOWN");

#ifdef WIN32
        nread = recvfrom(sfd, buf, bsize, 0,
                (SOCKADDR *) &peer_addr, &peer_addr_len);
        if (nread == SOCKET_ERROR) {
            REprintf("nvimcom: recvfrom failed with error %d\n", WSAGetLastError());
            return;
        }
#else
        nread = recvfrom(sfd, buf, bsize, 0,
                (struct sockaddr *) &peer_addr, &peer_addr_len);
        if (nread == -1){
            if(verbose > 1)
                REprintf("nvimcom: recvfrom failed\n");
            continue;     /* Ignore failed request */
        }
#endif

        int status;
        char *bbuf;

        if(verbose > 1){
            bbuf = buf;
            if(buf[0] < 30)
                bbuf++;
            REprintf("nvimcom Received: [%d] %s\n", buf[0], bbuf);
        }

        switch(buf[0]){
            case 1: // Set Editor server name or port number
                bbuf = buf;
                bbuf++;
                strcpy(edsrvr, bbuf);
                nvimcom_del_newline(edsrvr);
                sprintf(rep, "Editor server name set to %s\n", edsrvr);
                break;
            case 2: // Set Object Browser server name or port number
                bbuf = buf;
                bbuf++;
                objbr_auto = 1;
                strcpy(obsrvr, bbuf);
                nvimcom_del_newline(obsrvr);
                sprintf(rep, "Object Browser server name set to %s\n", obsrvr);
                break;
#ifdef WIN32
            case 3: // Set R as busy
                r_is_busy = 1;
                strcpy(rep, "R set as busy.");
                break;
#endif
            case 4: // Stop automatic update of Object Browser info
                objbr_auto = 0;
                break;
            case 5: // Update Object Browser (.GlobalEnv and Libraries)
#ifdef WIN32
                if(r_is_busy){
                    strcpy(rep, "R is busy.");
                } else {
                    if(buf[1] == 'B' || buf[1] == 'G')
                        nvimcom_list_env();
                    if(buf[1] == 'B' || buf[1] == 'L')
                        nvimcom_list_libs();
                }
#else
                if(buf[1] == 'B' || buf[1] == 'G')
                    flag_lsenv = 1;
                if(buf[1] == 'B' || buf[1] == 'L')
                    flag_lslibs = 1;
                nvimcom_fire();
#endif
                break;
            case 6: // Toggle list status
#ifdef WIN32
                if(r_is_busy){
                    strcpy(rep, "R is busy.");
                    break;
                }
#endif
                bbuf = buf;
                bbuf++;
                nvimcom_toggle_list_status(bbuf);
                if(strstr(bbuf, "package:") == bbuf){
                    openclosel = 1;
#ifdef WIN32
                    toggling_list = 1;
                    nvimcom_list_libs();
#else
                    flag_lslibs = 1;
#endif
                } else {
                    nobjs = 0;
#ifdef WIN32
                    nvimcom_list_env();
#else
                    flag_lsenv = 1;
#endif
                }
#ifndef WIN32
                nvimcom_fire();
#endif
                strcpy(rep, "OK");
                break;
            case 7: // Close/open all lists
#ifdef WIN32
                if(r_is_busy){
                    strcpy(rep, "R is busy.");
                    break;
                }
                toggling_list = 1;
#endif
                bbuf = buf;
                bbuf++;
                status = atoi(bbuf);
                ListStatus *tmp = firstList;
                if(status == 1 || status == 3){
                    while(tmp){
                        if(strstr(tmp->key, "package:") != tmp->key)
                            tmp->status = 1;
                        tmp = tmp->next;
                    }
                    nobjs = 0;
#ifdef WIN32
                    nvimcom_list_env();
#else
                    flag_lsenv = 1;
#endif
                } else {
                    while(tmp){
                        tmp->status = 0;
                        tmp = tmp->next;
                    }
                    openclosel = 1;
                    nobjs = 0;
#ifdef WIN32
                    nvimcom_list_libs();
                    nvimcom_list_env();
#else
                    flag_lsenv = 1;
                    flag_lslibs = 1;
#endif
                }
#ifdef WIN32
                if(status > 1 && obsrvr[0] != 0)
                    nvimcom_nvimclient("UpdateOB('both')", obsrvr);
#else
                nvimcom_fire();
#endif
                break;
            case 8: // eval expression
                bbuf = buf;
                bbuf++;
                if(strstr(bbuf, getenv("NVIMR_ID")) == bbuf){
                    bbuf += strlen(getenv("NVIMR_ID"));
#ifdef WIN32
                    if(r_is_busy)
                        strcpy(rep, "R is busy.");
                    else
                        nvimcom_eval_expr(bbuf);
#else
                    strncpy(flag_eval, bbuf, 510);
                    nvimcom_fire();
#endif
                } else {
                    REprintf("\nvimcom: received invalid NVIMR_ID.\n");
                }
                break;
            default: // do nothing
                REprintf("\nError [nvimcom]: Invalid message received: %s\n", buf);
                break;
        }

        nsent = strlen(rep);
        if (sendto(sfd, rep, nsent, 0, (struct sockaddr *) &peer_addr, peer_addr_len) != nsent)
            REprintf("Error sending response. [nvimcom]\n");

        if(verbose > 1)
            REprintf("nvimcom sent: %s\n", rep);

    }
#ifdef WIN32
    REprintf("nvimcom: Finished receiving. Closing socket.\n");
    result = closesocket(sfd);
    if (result == SOCKET_ERROR) {
        REprintf("closesocket failed with error %d\n", WSAGetLastError());
        return;
    }
    WSACleanup();
    return;
#else
    return(NULL);
#endif
}


void nvimcom_Start(int *vrb, int *odf, int *ols, int *anm, int *alw, int *lbe, char **pth, char **vcv)
{
    verbose = *vrb;
    opendf = *odf;
    openls = *ols;
    allnames = *anm;
    always_ls_env = *alw;
    labelerr = *lbe;

    R_PID = getpid();
    strncpy(nvimcom_version, *vcv, 31);

    if(getenv("NVIMR_TMPDIR")){
        strncpy(nvimcom_home, *pth, 1023);
        strncpy(tmpdir, getenv("NVIMR_TMPDIR"), 500);
        if(getenv("NVIMR_SECRET"))
            strncpy(nvimsecr, getenv("NVIMR_SECRET"), 127);
        else
            REprintf("nvimcom: Environment variable NVIMR_SECRET is missing.\n");
        char *srvr = getenv("NVIMR_SVRNM");
        if(srvr){
            if(strstr(srvr, "Neovim_") == NULL){
                REprintf("Warning: this version of nvimcom was built only for Neovim.\nThere is no support for either X11 or Windows 'clientserver' feature.\n");
            } else {
                Neovim = 1;
                if(verbose > 1)
                    Rprintf("R called by Neovim\n");
            }
        } else {
            if(verbose > -1)
                REprintf("nvimcom: Vim's server is unknown.\n");
        }
        if(verbose > 1)
            Rprintf("nvimcom: NVIMR_SVRNM=%s\n", srvr);
    } else {
        if(verbose)
            REprintf("nvimcom: It seems that R was not started by Vim. The communication with Vim-R-plugin will not work.\n");
        tmpdir[0] = 0;
        return;
    }

    snprintf(liblist, 510, "%s/liblist_%s", tmpdir, getenv("NVIMR_ID"));
    snprintf(globenv, 510, "%s/globenv_%s", tmpdir, getenv("NVIMR_ID"));

    char envstr[1024];
    envstr[0] = 0;
    if(getenv("LC_MESSAGES"))
        strcat(envstr, getenv("LC_MESSAGES"));
    if(getenv("LC_ALL"))
        strcat(envstr, getenv("LC_ALL"));
    if(getenv("LANG"))
        strcat(envstr, getenv("LANG"));
    int len = strlen(envstr);
    for(int i = 0; i < len; i++)
        envstr[i] = toupper(envstr[i]);
    if(strstr(envstr, "UTF-8") != NULL || strstr(envstr, "UTF8") != NULL){
        nvimcom_is_utf8 = 1;
        strcpy(strL, "\xe2\x94\x94\xe2\x94\x80 ");
        strcpy(strT, "\xe2\x94\x9c\xe2\x94\x80 ");
    } else {
        nvimcom_is_utf8 = 0;
        strcpy(strL, "`- ");
        strcpy(strT, "|- ");
    }

#ifndef WIN32
    *flag_eval = 0;
    int fds[2];
    if(pipe(fds) == 0){
        ifd = fds[0];
        ofd = fds[1];
        ih = addInputHandler(R_InputHandlers, ifd, &nvimcom_uih, 32);
    } else {
        REprintf("setwidth error: pipe != 0\n");
        ih = NULL;
    }
#endif

#ifdef WIN32
    tid = _beginthread(nvimcom_server_thread, 0, NULL);
#else
    pthread_create(&tid, NULL, nvimcom_server_thread, NULL);
#endif

    if(nvimcom_failure == 0){
        // Linked list sentinel
        firstList = calloc(1, sizeof(ListStatus));
        firstList->key = (char*)malloc(13 * sizeof(char));
        strcpy(firstList->key, "package:base");

        for(int i = 0; i < 64; i++){
            loadedlibs[i] = (char*)malloc(64 * sizeof(char));
            loadedlibs[i][0] = 0;
        }
        for(int i = 0; i < 64; i++){
            builtlibs[i] = (char*)malloc(64 * sizeof(char));
            builtlibs[i][0] = 0;
        }

        Rf_addTaskCallback(nvimcom_task, NULL, free, "NVimComHandler", NULL);

        nvimcom_initialized = 1;
        if(verbose > 0)
            // TODO: use packageStartupMessage()
            REprintf("nvimcom %s loaded\n", nvimcom_version);
        if(verbose > 1)
            REprintf("    NVIMR_TMPDIR = %s\n    NVIMR_ID = %s\n",
                    tmpdir, getenv("NVIMR_ID"));
    }
}

void nvimcom_Stop()
{
#ifndef WIN32
    if(ih){
        removeInputHandler(&R_InputHandlers, ih);
        close(ifd);
        close(ofd);
    }
#endif

    if(nvimcom_initialized){
        Rf_removeTaskCallbackByName("NVimComHandler");
#ifdef WIN32
        closesocket(sfd);
        WSACleanup();
#else
        close(sfd);
        pthread_cancel(tid);
#endif
        ListStatus *tmp = firstList;
        while(tmp){
            firstList = tmp->next;
            free(tmp->key);
            free(tmp);
            tmp = firstList;
        }
        for(int i = 0; i < 64; i++){
            free(loadedlibs[i]);
            loadedlibs[i] = NULL;
        }
        if(verbose)
            REprintf("nvimcom stopped\n");
    }
    nvimcom_initialized = 0;
}
