// SofaControl — Xbox controller as a virtual mouse for HTPC use.

#include "ControllerWindow.h"

#include <Windows.h>

#if defined(_M_X64)
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE /*prevInstance*/, PWSTR /*cmdLine*/, int /*showCmd*/) {
    ControllerWindow app;

    if (!app.Create(instance)) {
        MessageBoxW(nullptr, L"Could not create the SofaControl window.", L"Error", MB_ICONERROR);
        return 1;
    }

    return app.Run();
}
