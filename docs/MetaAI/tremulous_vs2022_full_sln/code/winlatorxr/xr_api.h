#ifndef XR_API_H
#define XR_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include <windows.h>

typedef struct {
    float hmd_q[4];
    float hmd_pos[3];
    float hmd_alt;
    float left_q[4], right_q[4];
    float left_pos[3], right_pos[3];
    float left_stick[2], right_stick[2];
    float grip_l_q[4], grip_r_q[4];
    float ipd, fov_h, fov_v;
    int sync;
    char buttons[32];
    int active;
    int has_alt, has_grip;
} xr_state_t;

extern xr_state_t g_xr;
extern CRITICAL_SECTION g_xr_cs;

void XR_Init(void);
void XR_Shutdown(void);
void XR_SendOut(int mode3d);
int XR_WaitForNewSync(int oldSync, int timeoutMs);
void QuatToEuler(float qx,float qy,float qz,float qw, float out[3]);

#ifdef __cplusplus
}
#endif

#endif
