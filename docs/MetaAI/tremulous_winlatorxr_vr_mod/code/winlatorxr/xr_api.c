#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "xr_api.h"

xr_state_t g_xr = {0};
static SOCKET s_in = INVALID_SOCKET, s_out = INVALID_SOCKET;
static HANDLE hThread = NULL;
static CRITICAL_SECTION cs;
static volatile int keepRunning = 0;

void QuatToEuler(float qx,float qy,float qz,float qw, float out[3]){
    float sinr = 2.0f*(qw*qx + qy*qz);
    float cosr = 1.0f - 2.0f*(qx*qx + qy*qy);
    float roll = atan2f(sinr, cosr);
    float sinp = 2.0f*(qw*qy - qz*qx);
    float pitch;
    if (fabsf(sinp) >= 1.0f) pitch = copysignf(3.14159265f/2.0f, sinp);
    else pitch = asinf(sinp);
    float siny = 2.0f*(qw*qz + qx*qy);
    float cosy = 1.0f - 2.0f*(qy*qy + qz*qz);
    float yaw = atan2f(siny, cosy);

    out[0] = -pitch * 57.29578f;
    out[1] = -yaw * 57.29578f;
    out[2] = roll * 57.29578f;
}

static void ensure_xr_dir(){
    CreateDirectoryA("Z:\\tmp", NULL);
    CreateDirectoryA("Z:\\tmp\\xr", NULL);
    CreateDirectoryA("D:\\xrtemp", NULL);
}
static void write_marker(){
    ensure_xr_dir();
    FILE *f = fopen("Z:\\tmp\\xr\\version","w");
    if(!f) f = fopen("D:\\xrtemp\\version","w");
    if(f){ fprintf(f,"0.5"); fclose(f); }
    f = fopen("Z:\\tmp\\xr\\vr","w");
    if(!f) f = fopen("D:\\xrtemp\\vr","w");
    if(f){ fprintf(f,"VR"); fclose(f); }
}
static void delete_marker(){
    remove("Z:\\tmp\\xr\\vr");
    remove("D:\\xrtemp\\vr");
}

static int isButtonString(const char *s){
    if(!s) return 0;
    size_t len = strlen(s);
    if(len < 5 || len > 32) return 0;
    for(size_t i=0;i<len;i++) if(s[i]!='T' && s[i]!='F') return 0;
    return 1;
}

static DWORD WINAPI xr_recv_thread(LPVOID _unused){
    char buf[4096];
    struct sockaddr_in from;
    int fromlen = sizeof(from);
    float floats[64];
    while(keepRunning){
        int n = recvfrom(s_in, buf, sizeof(buf)-1, 0, (struct sockaddr*)&from, &fromlen);
        if(n<=0) continue;
        buf[n]=0;

        // tokenize by space
        char *ctx=NULL;
        char *tok = strtok_s(buf, " \r\n\t", &ctx);
        int fcount=0;
        char btn[64]="";

        // skip optional "client0" header (non-float first token)
        if(tok){
            char *e; strtod(tok,&e);
            if(e==tok){
                tok = strtok_s(NULL," \r\n\t",&ctx);
            }
        }

        while(tok && fcount<64){
            if(isButtonString(tok)){
                strcpy_s(btn,sizeof(btn),tok);
                break;
            }
            // if contains letters and not just T/F, it's vendor string - stop
            int hasAlpha=0;
            for(char *p=tok;*p;p++) if(isalpha((unsigned char)*p) && *p!='T' && *p!='F' && *p!='e' && *p!='E'){ hasAlpha=1; break; }
            if(hasAlpha && strlen(tok)>4) break;

            char *e;
            double v = strtod(tok,&e);
            if(e!=tok){
                floats[fcount++] = (float)v;
            }
            tok = strtok_s(NULL," \r\n\t",&ctx);
        }
        if(fcount < 29) continue; // need 28 floats + sync

        EnterCriticalSection(&cs);
        g_xr.left_q[0]=floats[0]; g_xr.left_q[1]=floats[1]; g_xr.left_q[2]=floats[2]; g_xr.left_q[3]=floats[3];
        g_xr.left_stick[0]=floats[4]; g_xr.left_stick[1]=floats[5];
        g_xr.left_pos[0]=floats[6]; g_xr.left_pos[1]=floats[7]; g_xr.left_pos[2]=floats[8];
        g_xr.right_q[0]=floats[9]; g_xr.right_q[1]=floats[10]; g_xr.right_q[2]=floats[11]; g_xr.right_q[3]=floats[12];
        g_xr.right_stick[0]=floats[13]; g_xr.right_stick[1]=floats[14];
        g_xr.right_pos[0]=floats[15]; g_xr.right_pos[1]=floats[16]; g_xr.right_pos[2]=floats[17];
        g_xr.hmd_q[0]=floats[18]; g_xr.hmd_q[1]=floats[19]; g_xr.hmd_q[2]=floats[20]; g_xr.hmd_q[3]=floats[21];
        g_xr.hmd_pos[0]=floats[22]; g_xr.hmd_pos[1]=floats[23]; g_xr.hmd_pos[2]=floats[24];
        g_xr.ipd = floats[25]; g_xr.fov_h = floats[26]; g_xr.fov_v = floats[27];
        g_xr.sync = (int)floats[28];

        g_xr.has_alt=0; g_xr.has_grip=0;
        if(fcount>=30){ g_xr.hmd_alt=floats[29]; g_xr.has_alt=1; }
        if(fcount>=34){
            g_xr.grip_l_q[0]=floats[30]; g_xr.grip_l_q[1]=floats[31]; g_xr.grip_l_q[2]=floats[32]; g_xr.grip_l_q[3]=floats[33];
            g_xr.has_grip=1;
        }
        if(fcount>=38){
            g_xr.grip_r_q[0]=floats[34]; g_xr.grip_r_q[1]=floats[35]; g_xr.grip_r_q[2]=floats[36]; g_xr.grip_r_q[3]=floats[37];
        }
        if(btn[0]) strcpy_s(g_xr.buttons,sizeof(g_xr.buttons),btn);
        LeaveCriticalSection(&cs);
    }
    return 0;
}

void XR_Init(){
    if(g_xr.active) return;
    InitializeCriticalSection(&cs);
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2),&wsa);
    write_marker();

    s_in = socket(AF_INET,SOCK_DGRAM,0);
    s_out = socket(AF_INET,SOCK_DGRAM,0);
    int tv=100;
    setsockopt(s_in, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));

    struct sockaddr_in addr={0};
    addr.sin_family=AF_INET;
    addr.sin_port=htons(7872);
    addr.sin_addr.s_addr=INADDR_ANY;
    bind(s_in,(struct sockaddr*)&addr,sizeof(addr));

    keepRunning=1;
    g_xr.active=1;
    g_xr.grip_l_q[3]=1.0f;
    g_xr.grip_r_q[3]=1.0f;
    g_xr.hmd_q[3]=1.0f;
    hThread = CreateThread(NULL,0,xr_recv_thread,NULL,0,NULL);
}

void XR_Shutdown(){
    if(!g_xr.active) return;
    keepRunning=0;
    g_xr.active=0;
    if(hThread){ WaitForSingleObject(hThread,1000); CloseHandle(hThread); hThread=NULL; }
    if(s_in!=INVALID_SOCKET){ closesocket(s_in); s_in=INVALID_SOCKET; }
    if(s_out!=INVALID_SOCKET){ closesocket(s_out); s_out=INVALID_SOCKET; }
    delete_marker();
    DeleteCriticalSection(&cs);
    WSACleanup();
}

void XR_SendOut(int mode3d){
    if(s_out==INVALID_SOCKET) return;
    struct sockaddr_in dst={0};
    dst.sin_family=AF_INET;
    dst.sin_port=htons(7278);
    dst.sin_addr.s_addr = inet_addr("127.0.0.1");
    char out[64];
    snprintf(out,sizeof(out),"0 0 1 %d 0 0", mode3d); // mono=0
    sendto(s_out,out,(int)strlen(out),0,(struct sockaddr*)&dst,sizeof(dst));
}

int XR_WaitForNewSync(int oldSync,int timeoutMs){
    int t=0;
    while(t<timeoutMs){
        EnterCriticalSection(&cs);
        int s=g_xr.sync;
        LeaveCriticalSection(&cs);
        if(s!=oldSync) return s;
        Sleep(2);
        t+=2;
    }
    return oldSync;
}
