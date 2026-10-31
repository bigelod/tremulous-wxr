// PATCH: code/client/cl_main.c
#include "../winlatorxr/xr_api.h"

void CL_Init(void){
    // ... existing ...
    XR_Init();
}
void CL_Shutdown(void){
    XR_Shutdown();
    // ... existing ...
}

// Also in code/client/cl_input.c - optional controller mapping
// Example inside CL_CreateNewCommands():
if(g_xr.active){
    // left stick = move
    cmd->forwardmove += (int)(g_xr.left_stick[1] * 127);
    cmd->rightmove += (int)(g_xr.left_stick[0] * 127);
    // right stick = look
    cl.viewangles[YAW] -= g_xr.right_stick[0] * 2.0f;
    cl.viewangles[PITCH] += g_xr.right_stick[1] * 2.0f;
    // right trigger (check buttons[?]) -> +attack
    if(strchr(g_xr.buttons,'T')){
        // map button 7 = R_TRIGGER etc. Index per spec:
        // 0 L_GRIP,1 L_MENU,2 L_THUMB_PRESS,3 L_LEFT,4 L_RIGHT,5 L_UP,6 L_DOWN,7 L_TRIGGER,8 L_X,9 L_Y,
        // 10 R_A,11 R_B,12 R_GRIP,13 R_THUMB_PRESS,14 R_LEFT,15 R_RIGHT,16 R_UP,17 R_DOWN,18 R_TRIGGER
        if(strlen(g_xr.buttons)>18 && g_xr.buttons[18]=='T') cmd->buttons |= BUTTON_ATTACK;
    }
}
