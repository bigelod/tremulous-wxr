// PATCH: code/renderer/tr_main.c or code/renderer/tr_backend.c
// Inside RE_EndFrame() / RB_EndFrame() before SwapBuffers / GLimp_EndFrame

#include "../winlatorxr/xr_api.h"

// draw red sync pixel for WinlatorXR frame pacing
if(g_xr.active){
    qglDisable(GL_DEPTH_TEST);
    qglDisable(GL_TEXTURE_2D);
    qglMatrixMode(GL_PROJECTION);
    qglPushMatrix();
    qglLoadIdentity();
    qglOrtho(0, glConfig.vidWidth, glConfig.vidHeight, 0, -1, 1);
    qglMatrixMode(GL_MODELVIEW);
    qglPushMatrix();
    qglLoadIdentity();
    // sRGB red = sync/255
    qglColor3f(g_xr.sync / 255.0f, 0.0f, 0.0f);
    qglBegin(GL_QUADS);
    qglVertex2f(0, glConfig.vidHeight-10);
    qglVertex2f(10, glConfig.vidHeight-10);
    qglVertex2f(10, glConfig.vidHeight);
    qglVertex2f(0, glConfig.vidHeight);
    qglEnd();
    qglPopMatrix();
    qglMatrixMode(GL_PROJECTION);
    qglPopMatrix();
    qglMatrixMode(GL_MODELVIEW);
    // mono VR = 0
    XR_SendOut(0);
}
