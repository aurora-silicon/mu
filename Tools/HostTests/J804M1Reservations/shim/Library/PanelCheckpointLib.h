#ifndef HOST_SHIM_PANEL_H_
#define HOST_SHIM_PANEL_H_
#include <Base.h>
extern int gHostLastError;
static inline VOID PanelCheckpointError (UINTN Code) { gHostLastError = (int)Code; }
#endif
