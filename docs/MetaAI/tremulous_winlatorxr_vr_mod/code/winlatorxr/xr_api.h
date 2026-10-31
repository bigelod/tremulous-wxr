#ifndef XR_API_H
#define XR_API_H

// WinlatorXR XrAPI v0.3-v0.5 monocular VR
// Works with Z:\tmp\xr and UDP 7872/7278

typedef struct {
    float hmd_q[4]; // x,y,z,w OpenXR (+Y up, -Z forward, meters)
    float hmd_pos[3];
    float hmd_alt; // v0.5: floor altitude
    float left_q[4], right_q[4];
    float left_pos[3], right_pos[3];
    float left_stick[2], right_stick[2];
    float grip_l_q[4], grip_r_q[4]; // v0.5
    float ipd, fov_h, fov_v;
    int sync; // 0-255 frame id
    char buttons[32]; // T/F string
    int active;
    int has_alt, has_grip;
} xr_state_t;

extern xr_state_t g_xr;

void XR_Init(void);
void XR_Shutdown(void);
void XR_SendOut(int mode3d); // 0=mono, 1=SBS, 2=AER
int XR_WaitForNewSync(int oldSync, int timeoutMs);
void QuatToEuler(float qx,float qy,float qz,float qw, float out[3]); // out = pitch,yaw,roll deg

#endif
