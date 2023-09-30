// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

#include <ModUtils.h>

/*
armoredcore6.exe+16C8259 - E8 3E819701           - call armoredcore6.exe+304039C
armoredcore6.exe+16C825E - 0F28 D8               - movaps xmm3,xmm0
armoredcore6.exe+16C8261 - F3 0F10 05 0F444402   - movss xmm0,[armoredcore6.exe+3B0C678]
armoredcore6.exe+16C8269 - 0F2F C3               - comiss xmm0,xmm3
armoredcore6.exe+16C826C - 72 16                 - jb armoredcore6.exe+16C8284
armoredcore6.exe+16C826E - 48 8B 05 8B3A8A03     - mov rax,[armoredcore6.exe+4F6BD00]
armoredcore6.exe+16C8275 - 0F28 74 24 40         - movaps xmm6,[rsp+40]
armoredcore6.exe+16C827A - 0F28 7C 24 30         - movaps xmm7,[rsp+30]
armoredcore6.exe+16C827F - 48 83 C4 58           - add rsp,58
armoredcore6.exe+16C8283 - C3                    - ret 
armoredcore6.exe+16C8284 - F3 0F10 25 44ABB302   - movss xmm4,[armoredcore6.exe+4202DD0]

*/
char PATTERN_DEADZONE[] = "0f28d8f30f1005????????0f2fc3";

DWORD WINAPI MainThread(LPVOID lparam)
{
    Sleep(2000);

    std::vector<LPVOID> scan = MemPatternScan(nullptr, StringtoScanPattern(PATTERN_DEADZONE), false, 1);
    if (scan.size())
    {
        uintptr_t ins = reinterpret_cast<uintptr_t>(scan[0]) + 3;
        int offset = *reinterpret_cast<int*>(ins + 4);
        float* pDeadzone = reinterpret_cast<float*>(ins + 8 + offset);
        ULog::Get().println("original deadzone: %f", *pDeadzone);
        *pDeadzone = 0.f;
    }

    return 0;
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        ULog::FileName = "AC6Deadzone.log";
        CreateThread(0, 0, &MainThread, 0, 0, 0);
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

