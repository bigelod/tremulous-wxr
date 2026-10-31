// PATCH: code/cgame/cg_view.c
// Add at top:
#include "../winlatorxr/xr_api.h"
extern cvar_t *vr_enable;
extern cvar_t *vr_posScale;

// In CG_RegisterCvars() or CG_Init:
vr_enable = trap_Cvar_Get("vr_enable", "1", CVAR_ARCHIVE);
vr_posScale = trap_Cvar_Get("vr_posScale", "40", CVAR_ARCHIVE);

// Inside CG_CalcViewValues() - after normal vieworg/viewangles calc, before return:
if(vr_enable && vr_enable->integer && g_xr.active){
    float euler[3];
    float q[4], pos[3], alt=0;
    EnterCriticalSection(&cs);
    // copy under lock - include cs as extern if needed, or use g_xr directly if you trust volatile
    q[0]=g_xr.hmd_q[0]; q[1]=g_xr.hmd_q[1]; q[2]=g_xr.hmd_q[2]; q[3]=g_xr.hmd_q[3];
    pos[0]=g_xr.hmd_pos[0]; pos[1]=g_xr.hmd_pos[1]; pos[2]=g_xr.hmd_pos[2];
    alt=g_xr.hmd_alt;
    LeaveCriticalSection(&cs);
    QuatToEuler(q[0],q[1],q[2],q[3], euler);
    cg.refdefViewAngles[YAW] += euler[1];
    cg.refdefViewAngles[PITCH] += euler[0];
    cg.refdefViewAngles[ROLL] += euler[2];

    float scale = vr_posScale ? vr_posScale->value : 40.0f;
    vec3_t fwd, right, up;
    AngleVectors(cg.refdefViewAngles, fwd, right, up);
    // OpenXR: X right, Y up, Z back -> Q3 forward
    VectorMA(cg.refdef.vieworg, pos[0]*scale, right, cg.refdef.vieworg);
    VectorMA(cg.refdef.vieworg, -pos[2]*scale, fwd, cg.refdef.vieworg);
    cg.refdef.vieworg[2] += (pos[1]+alt)*scale;
}

// NOTE: you need to expose CRITICAL_SECTION cs from xr_api.c or duplicate lock.
// Simplest: add in xr_api.h: extern CRITICAL_SECTION g_xr_cs;
