// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

#define GLM_FORCE_SWIZZLE 1
#include <glm/glm.hpp>

#include <ModUtils.h>
#include <MathUtils.h>
#include <INIReader.h>

#include <luajit/lua.hpp>

#include <set>

#pragma comment(lib, "minhook.x64.lib")

#define CATIMPL(a, b) a ## b
#define CAT(a, b) CATIMPL(a, b)
#define SEQ(pref) CAT(pref, __COUNTER__)
#define PAD(size) char SEQ(_padding) [size];

template<typename T>
bool vector_contains(std::vector<T>& v, T needle)
{
    for (T elem : v)
    {
        if (elem == needle)
        {
            return true;
        }
    }
    return false;
}

template<typename T>
int vector_indexof(std::vector<T>& v, T needle)
{
    int i = 0;
    for (T elem : v)
    {
        if (elem == needle)
        {
            return i;
        }
        ++i;
    }
    return -1;
}

HMODULE g_hModule;
lua_State *g_Lua;

struct FConfig
{
    float FovMul = 1.0f;
    float distmul = 1.0f;
    float xoffset = 0.0f;
    float lockonOffsetMul = 1.f;
    float yOffset = 0.f;
    float PivotInterpMul = 1.f;
    float PivotInterpMulZ = 0.f;

    glm::vec3 Offset = { 0.f, 0, 0 };

    std::string DistLua = "d";

    void Read(std::string path)
    {
        INIReader ini(path);
        if (ini.ParseError() != 0)
        {
            ULog::Get().println("Cannot read config file.");
            return;
        }
        FovMul = ini.GetFloat("common", "fov-multiplier", FovMul);
        //distmul = ini.GetFloat("main", "distance-multiplier", distmul);
        yOffset = ini.GetFloat("common", "y-offset", yOffset);
        xoffset = ini.GetFloat("free-aim", "x-offset", xoffset);
        lockonOffsetMul = ini.GetFloat("lock-on", "x-offset-multiplier", lockonOffsetMul);
        PivotInterpMul = ini.GetFloat("pivot", "speed-multiplier", PivotInterpMul);
        PivotInterpMulZ = ini.GetFloat("pivot", "speed-multiplier-z", PivotInterpMulZ);
        DistLua = ini.Get("common", "distance", DistLua);
        //Offset = ini.GetVec("common", "freeaim-offset", Offset);

        if (!(PivotInterpMulZ > 0.f))
        {
            PivotInterpMulZ = PivotInterpMul;
        }
    }
} Config;

struct FCameraData
{
    // float RotationZ; // +1F0, when dashing
    // float interpZ; // +328
    // float interpXY; // +358, maybe just the x axis

    PAD(0x10);
    glm::mat4 Transform;
    PAD(0x1E0 - 0x50);
    union {
        PAD(0x10);
        glm::vec2 RotationEuler; // x: pitch, y: yaw
    };
    PAD(0x215 - 0x1F0);
    bool bHasLockon; // +215
    PAD(0x328 - 0x216);
    float InterpSpeedZ;
    PAD(0x354 - (0x328 + 4));
    float InterpSpeedX;
};
FCameraData* pCameraData;

enum struct EScalarOffset : INT_PTR
{
    FOV = 0x50,
    X_OFFSET = 0x220,
    Y_OFFSET = 0x39C,
    MAX_DISTANCE = 0x2F4,
};

float LockonInterp;

void (*tram_CameraTick)(LPVOID, float, LPVOID, LPVOID);
void hk_CameraTick(FCameraData* pCamData, float frametime, LPVOID r8, LPVOID r9)
{
    pCameraData = pCamData;
    LockonInterp = InterpToF(LockonInterp, float(pCamData->bHasLockon), 1.f, frametime);
    tram_CameraTick(pCamData, frametime, r8, r9);

    //pCameraData->Transform[3].y += Config.yOffset;

    pCameraData = nullptr;
}

extern "C"
{
    void prehk_ScalarInterp();
    LPVOID ScalarInterpReturn;
}

constexpr int ICALLER_INTERP_XOFFSET = 1;
constexpr int ICALLER_INTERP_MAXDIST = 2;
constexpr int ICALLER_INTERP_FOV = 5;

UINT_PTR InterpCallerMaxDist;
UINT_PTR InterpCallerFoV;
UINT_PTR InterpCallerXOffset;

// interpolation function called for many scalar properties, fov, x offset, etc.
float (*tram_ScalarInterp)(LPVOID, float, float, BYTE, float, float);

extern "C" float hk_ScalarInterp(LPVOID pIntermediates/*?*/, float target, float speed, BYTE r9/*is 0?*/, float f, float f1)
{
    static std::vector<LPVOID> callers;

    if (!vector_contains(callers, ScalarInterpReturn))
    {
        ULog::Get().dprintln("ScalarInterp caller: %p %d", ScalarInterpReturn, callers.size());
        callers.push_back(ScalarInterpReturn);
    }

    if (pCameraData)
    {
        INT_PTR relAddress = UINT_PTR(pIntermediates) - UINT_PTR(pCameraData);

        switch (EScalarOffset(relAddress))
        {
        case EScalarOffset::FOV:
            target *= Config.FovMul;
            break;
        case EScalarOffset::X_OFFSET:
            //target += pCameraData->bHasLockon ? 0.f : Config.xoffset;
            break;
        case EScalarOffset::Y_OFFSET:
            //target += Config.yOffset;
            break;
        case EScalarOffset::MAX_DISTANCE:
        {

            //target *= Config.distmul;
            break;
        }
        default:
            break;
        }

        //switch (vector_indexof(callers, ScalarInterpReturn))
        //{
        //case ICALLER_INTERP_FOV:
        //    target *= Config.FovMul;
        //    break;
        //case ICALLER_INTERP_MAXDIST:
        //    target *= Config.distmul;
        //    break;
        //case ICALLER_INTERP_XOFFSET:
        //    //if (!pCameraData->bHasLockon)
        //    //{
        //    //    target = Config.xoffset;
        //    //}
        //    break;
        //default:
        //    break;
        //}
    }

    // TDOO: replace these with intermediates memory offset and remove the memory scans for function callers
    if (UINT_PTR(ScalarInterpReturn) == InterpCallerFoV)
    {
        //target *= Config.FovMul;
    }
    if (UINT_PTR(ScalarInterpReturn) == InterpCallerMaxDist)
    {
        //target *= Config.distmul;
    }

    float out = tram_ScalarInterp(pIntermediates, target, speed, r9, f, f1);

    //if (UINT_PTR(ScalarInterpReturn) == InterpCallerXOffset && !pCameraData->bHasLockon)
    //{
    //    out = lerp(Config.xoffset, out, LockonInterp);
    //}

    return out;
}

extern "C"
{
    void prehk_GetInterpResult();
    LPVOID RA_GetInterpResult;
}

UINT_PTR ACallerInterpReXOffsetNoLockon;
UINT_PTR ACallerInterpReXOffset;

// sometimes called right after the interp function
float (*tram_GetInterpResult)(LPVOID);
extern "C" float hk_GetInterpResult(float* p)
{
    //static std::vector<LPVOID> callers;

    //if (vector_indexof(callers, RA_GetInterpResult) < 0)
    //{
    //    ULog::Get().dprintln("GetInterpResult caller: %p %d", RA_GetInterpResult, callers.size());
    //    callers.push_back(RA_GetInterpResult);
    //}


    float out = tram_GetInterpResult(p);
    if (pCameraData)
    {
        INT_PTR relAddress = UINT_PTR(p) - UINT_PTR(pCameraData);

        switch (EScalarOffset(relAddress))
        {
        //case EScalarOffset::Y_OFFSET:
        //    return out;
        //case EScalarOffset::FOV:
        //    return out * Config.FovMul;
        case EScalarOffset::MAX_DISTANCE:
        {
            lua_pushnumber(g_Lua, out);
            lua_setglobal(g_Lua, "d");
            lua_pushnumber(g_Lua, pCameraData->RotationEuler.x);
            lua_setglobal(g_Lua, "pitch");
            std::string script = "out=(" + Config.DistLua + ")";
            if (luaL_loadbuffer(g_Lua, script.c_str(), script.size(), "dist") || lua_pcall(g_Lua, 0, 0, 0))
            {
                ULog::Get().dprintln("lua error: %s", lua_tostring(g_Lua, -1));
                lua_pop(g_Lua, 1);
            }
            lua_getglobal(g_Lua, "out");
            out = lua_tonumber(g_Lua, -1);
            lua_pop(g_Lua, 1);
            return out;
        }
        case EScalarOffset::X_OFFSET:
            return lerp(Config.xoffset, out * Config.lockonOffsetMul, LockonInterp);
            break;
        default:
            break;
        }

        //switch (vector_indexof(callers, ScalarInterpReturn))
        //{
        //case ICALLER_INTERP_FOV:
        //    target *= Config.FovMul;
        //    break;
        //case ICALLER_INTERP_MAXDIST:
        //    target *= Config.distmul;
        //    break;
        //case ICALLER_INTERP_XOFFSET:
        //    //if (!pCameraData->bHasLockon)
        //    //{
        //    //    target = Config.xoffset;
        //    //}
        //    break;
        //default:
        //    break;
        //}
    }



    return out;
}

glm::vec4* (*tram_PivotPos)(LPVOID, LPVOID, LPVOID, float, float);
glm::vec4* hk_PivotPos(LPVOID rcx, LPVOID rdx, LPVOID r8, float xOffset, float maxDist)
{
    glm::vec4* pOut = tram_PivotPos(rcx, rdx, r8, xOffset, maxDist);
    //pOut->y += Config.yOffset;
    return pOut;
}

glm::vec4* (*tram_PivotPosInterp)(LPVOID, LPVOID, LPVOID, LPVOID, LPVOID, LPVOID, float, float);
glm::vec4* hk_PivotPosInterp(LPVOID rcx, LPVOID rdx, LPVOID r8, LPVOID r9, LPVOID s0, LPVOID s1, float s2, float s3)
{
    if (pCameraData)
    {
        pCameraData->InterpSpeedX *= Config.PivotInterpMul;
        pCameraData->InterpSpeedZ *= Config.PivotInterpMulZ;
    }
    return tram_PivotPosInterp(rcx, rdx, r8, r9, s0, s1, s2, s3);
}


/*
armoredcore6.exe+6C6297 - 4C 8D 8D A0110000     - lea r9,[rbp+000011A0]
armoredcore6.exe+6C629E - 41 0F28 D5            - movaps xmm2,xmm13
armoredcore6.exe+6C62A2 - 48 8D 95 10140000     - lea rdx,[rbp+00001410]
armoredcore6.exe+6C62A9 - 48 8B CF              - mov rcx,rdi
armoredcore6.exe+6C62AC - E8 0F730000           - call armoredcore6.exe+6CD5C0
*/
constexpr char PATTERN_FN_PIVOT_OFFSET[] = "4C 8D 8D A0110000 41 0F28 D5 48 8D 95 10140000 48 8B CF E8";

glm::vec4* (*tram_OffsetPivot)(LPVOID, LPVOID, float, LPVOID, LPVOID, LPVOID);
glm::vec4* hk_OffsetPivot(LPVOID rcx, LPVOID rdx, float xmm2, LPVOID r9, LPVOID s0, LPVOID s1)
{
    glm::vec4* out = tram_OffsetPivot(rcx, rdx, xmm2, r9, s0, s1);
    out->y += Config.yOffset;

    //glm::mat4 rotation = glm::rotate(glm::mat4(1), pCameraData->RotationEuler.y, glm::vec3(0.f, 1.f, 0.f));
    //out->xyz += glm::vec3(rotation * glm::vec4(Config.Offset, 1.f));

    return out;
}

bool CheckHook(LPVOID pTarget)
{
    LPVOID hkJmpTarget = GetJumpTargetFar(GetJumpTargetNear(pTarget));
    if (hkJmpTarget == nullptr)
    {
        return false;
    }
    //ULog::Get().println("hook target %p", hkJmpTarget);
    return IsCurrentModuleAddress(hkJmpTarget, g_hModule);
}

std::vector<UMinHook> Hooks;
//std::vector<UMinHook*> PersistChkHooks;

DWORD WINAPI ThreadCheckHook(LPVOID lpParam)
{
    for (;;)
    {
        for (UMinHook& hook : Hooks)
        {
            //ULog::Get().println("hook \"%s\" status: %s", hook.GetID(), CheckHook(hook.GetTarget()) ? "enabled" : "disabled");
            bool bEnabled = CheckHook(hook.GetTarget());
            if (!bEnabled && hook.IsEnabled())
            {
                ULog::Get().println("hook \"%s\" disabled unexpectedly. attempting to re-enable", hook.GetID());
                hook.DisableImpl();
                hook.EnableImpl();
            }
        }
        Sleep(1000);
    }

    return 0;
}

DWORD WINAPI MainThread(LPVOID lpParam)
{

    Sleep(3000);
    MH_Initialize();

    ULog::Get().println("config struct: %p", &Config);
    //Config.Read(TARGET_NAME ".ini");

    /* fov
        armoredcore6.exe+6D475E - 74 07                 - je armoredcore6.exe+6D4767
        armoredcore6.exe+6D4760 - E8 2B1F0100           - call armoredcore6.exe+6E6690
        armoredcore6.exe+6D4765 - EB 27                 - jmp armoredcore6.exe+6D478E
        armoredcore6.exe+6D4767 - F3 0F10 83 74080000   - movss xmm0,[rbx+00000874]
        armoredcore6.exe+6D476F - 44 0FB6 8B 78080000   - movzx r9d,byte ptr [rbx+00000878]
        armoredcore6.exe+6D4777 - F3 0F10 55 50         - movss xmm2,[rbp+50]
        armoredcore6.exe+6D477C - F3 44 0F11 5C 24 28   - movss [rsp+28],xmm11
        armoredcore6.exe+6D4783 - F3 0F11 44 24 20      - movss [rsp+20],xmm0
        armoredcore6.exe+6D4789 - E8 521F0100           - call armoredcore6.exe+6E66E0
        armoredcore6.exe+6D478E - 48 8B CE              - mov rcx,rsi
        armoredcore6.exe+6D4791 - E8 CA1E0100           - call armoredcore6.exe+
    */
    Hooks.push_back(
        UMinHook(
            "ScalarInterp",
            "F3 0F 10 55 50 F3 44 0F 11 5C 24 28 F3 0F 11 44 24 20 E8 ???????? 48 8B CE E8",
            18,
            &prehk_ScalarInterp,
            (LPVOID*)(&tram_ScalarInterp)
        )
    );

    Hooks.push_back(
        UMinHook(
            "GetInterpResult",
            "F3 0F 11 44 24 20 44 0F B6 8F 78 08 00 00 41 0F 28 D2 0F 28 CE 49 8B CE E8 ???????? 49 8B CE E8",
            32,
            &prehk_GetInterpResult,
            (LPVOID*)(&tram_GetInterpResult)
        )
    );

    //{
    //    std::vector scan = MemPatternScan(
    //        nullptr,
    //        StringtoScanPattern("F3 0F 11 44 24 20 44 0F B6 8F 78 08 00 00 41 0F 28 D2 0F 28 CE 49 8B CE E8 ???????? 49 8B CE E8"),
    //        false,
    //        1
    //    );
    //    if (scan.size())
    //    {
    //        InterpCallerXOffset = (reinterpret_cast<UINT_PTR>(scan[0]) + 29);
    //        ACallerInterpReXOffset = reinterpret_cast<UINT_PTR>(scan[0]) + 37;

    //    }
    //}
    //{
    //    std::vector scan = MemPatternScan(
    //        nullptr,
    //        StringtoScanPattern("E8 ???????? F3 0F 10 8F 18 02 00 00 F3 0F 5C C8 F3 0F 11 4C 24 30 8B 44 24 30 0F BA F0 1F"),
    //        false,
    //        1
    //    );
    //    if (scan.size())
    //    {
    //        ACallerInterpReXOffsetNoLockon = (reinterpret_cast<UINT_PTR>(scan[0]) + 5);
    //    }
    //} 
    //{
    //    std::vector scan = MemPatternScan(
    //        nullptr,
    //        StringtoScanPattern("E8 ???????? 48 8B 07 0F 57 FF 48 85 C0 74 0A"),
    //        false,
    //        1
    //    );
    //    if (scan.size())
    //    {
    //        InterpCallerMaxDist = (reinterpret_cast<UINT_PTR>(scan[0]) + 5);
    //    }
    //} 
    //{
    //    std::vector scan = MemPatternScan(
    //        nullptr,
    //        StringtoScanPattern("E8 ???????? 48 8B CE E8 CA 1E 01 00 F3 0F 58 83"),
    //        false,
    //        1
    //    );
    //    if (scan.size())
    //    {
    //        InterpCallerFoV = (reinterpret_cast<UINT_PTR>(scan[0]) + 5);
    //    }
    //}

    /*
        armoredcore6.exe+6CDC0F - F3 0F11 44 24 20      - movss [rsp+20],xmm0
        armoredcore6.exe+6CDC15 - 0F28 DF               - movaps xmm3,xmm7
        armoredcore6.exe+6CDC18 - 4C 8B C3              - mov r8,rbx
        armoredcore6.exe+6CDC1B - 48 8B D6              - mov rdx,rsi
        armoredcore6.exe+6CDC1E - 48 8D 4C 24 60        - lea rcx,[rsp+60]
        armoredcore6.exe+6CDC23 - E8 781E0000           - call armoredcore6.exe+6CFAA0
    */
    //Hooks.push_back(
    //    UMinHook(
    //        "PivotPos",
    //        "F3 0F 11 44 24 20 0F 28 DF 4C 8B C3 48 8B D6 48 8D 4C 24 60 E8",
    //        0x23 - 0x0f,
    //        &hk_PivotPos,
    //        (LPVOID*)(&tram_PivotPos)
    //    )
    //);

    Hooks.push_back(
        UMinHook(
            "PivotOffset",
            PATTERN_FN_PIVOT_OFFSET,
            0x15,
            &hk_OffsetPivot,
            (LPVOID*)(&tram_OffsetPivot)
        )
    );

    Hooks.push_back(
        UMinHook(
            "PivotPosInterp",
            "E8 ???????? 0F 28 00 66 0F 7F 85 B0 12 00 00 0F 28 9D E0 00 00 00 0F 29 5D 60",
            &hk_PivotPosInterp,
            (LPVOID*)(&tram_PivotPosInterp)
        )
    );

    // it's a virtual function so searching for the prologue seems to be the only option
    // the game also sometimes rewrites the prologue of this function...
    Hooks.push_back(
        UMinHook(
            "CameraTick",
            "48 8D AC 24 88 C2 FF FF B8 78 3E 00 00 E8 ???????? 48 2B E0",
            -13,
            &hk_CameraTick,
            (LPVOID*)(&tram_CameraTick)
        )
    );


    CreateThread(0, 0, &ThreadCheckHook, 0, 0, 0);

    return 0;
}

#define LS_IMPL(pf, s) pf##s
#define LS(s) LS_IMPL(L, s)

DWORD WINAPI ThreadConfigRead(LPVOID lpParam)
{
    static constexpr size_t FILE_NOTIFY_BUFFER_SIZE = 1024;

    std::string filename = TARGET_NAME ".ini";
    std::wstring wfilename = LS(TARGET_NAME) L".ini";

    Config.Read(filename);
    std::string modDir = GetDLLDirectory(g_hModule);
    while (1)
    {
        char notifybuf[FILE_NOTIFY_BUFFER_SIZE];
        DWORD numBytes;
        HANDLE hDir = CreateFileA(modDir.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
        if (ReadDirectoryChangesW(hDir, &notifybuf, FILE_NOTIFY_BUFFER_SIZE, FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE, &numBytes, 0, 0))
        {
            FILE_NOTIFY_INFORMATION* pNotify = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(&notifybuf);
            if (numBytes)
            {
                while (1)
                {
                    std::wstring wsName(pNotify->FileName, pNotify->FileNameLength / sizeof(WCHAR));
                    if (wsName == wfilename)
                    {
                        std::string filename;
                        std::transform(wsName.begin(), wsName.end(), std::back_inserter(filename), [](wchar_t c) {
                            return (char)c;
                            });
                        ULog::Get().println("Config file changed");
                        Sleep(300); // workaround for some editors like vscode
                        Config.Read(modDir + "\\" + filename);
                    }
                    if (pNotify->NextEntryOffset == 0) break;
                    pNotify += pNotify->NextEntryOffset;
                }
            }
            else
            {
                ULog::Get().println("Couldn't setup config file watch");
                break;
            }
        }
    }

    return 0;
}

#undef LS
#undef LS_IMPL

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        ULog::FileName = TARGET_NAME ".log";
        g_hModule = hModule;

        ULog& log = ULog::Get();
        HMODULE hMinhook = LoadLibraryA("minhook.x64.dll");
        if (hMinhook == NULL)
        {
            MessageBoxA(NULL, "Couldn't load MinHook.x64.dll, is it missing from the current directory?", TARGET_NAME, MB_OK);
            log.println("Couldn't load MinHook.x64.dll, aborting.");
            return FALSE;
        }
        g_Lua = lua_open();
        luaL_openlibs(g_Lua);

        DisableThreadLibraryCalls(hModule);
        CreateThread(0, 0, &MainThread, 0, 0, 0);
        CreateThread(0, 0, &ThreadConfigRead, 0, 0, 0);

    }
    if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        lua_close(g_Lua);
        MH_Uninitialize();
    }
    return TRUE;
}

