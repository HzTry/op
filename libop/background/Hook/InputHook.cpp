
#include "InputHook.h"
#include "../../core/opEnv.h"
#include "../../core/helpfunc.h"
#include "opMessage.h"
#include "../../winapi/query_api.h"
#include "MinHook.h"

#include "dinput.h"
#include <vector>

const GUID OP_IID_IDirectInput8W = {0xBF798031, 0x483A, 0x4DA2, 0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00};
const GUID OP_GUID_SysMouse = {0x6F1D2B60, 0xD5A0, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00};
/*target window hwnd*/
HWND InputHook::input_hwnd;
int InputHook::input_type;
/*name of ...*/
wchar_t InputHook::shared_res_name[256];
wchar_t InputHook::mutex_name[256];
void *InputHook::old_address;
//
opMouseState InputHook::m_mouseState;
bool InputHook::is_hooked = false;
bool InputHook::is_lock_mouse = true;

WNDPROC gRawWindowProc = 0;
std::vector<void *> gDinputVtb;
std::vector<void *> gDinputVtbRaw;
void* rawGetMessage;

const int indexAcquire = 7;
const int indexGetDeviceState = 9;
const int indexPoll = 25;

int getDinputVtb();

HRESULT __stdcall hkAcquire(IDirectInputDevice8W *this_);

HRESULT __stdcall hkPoll(IDirectInputDevice8W *this_);

HRESULT __stdcall hkGetDeviceState(IDirectInputDevice8W *this_, DWORD size, LPVOID ptr);
BOOL __stdcall hkGetMessage(LPMSG lpMsg,HWND hWnd,UINT wMsgFilterMin,UINT wMsgFilterMax);

LRESULT CALLBACK opWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

bool isMouseMessage(UINT message);

int InputHook::setup(HWND hwnd_, int input_type_)
{
    if (!IsWindow(hwnd_))
        return 0;
    opEnv::m_showErrorMsg = 2; //write data to file
    setlog("SetInputHook");

    bool ret = false;
    if (input_type_ == INPUT_TYPE::IN_DX) {
        ret = mouseDxHook(hwnd_, input_type_);
    }
    else if (input_type_ == INPUT_TYPE::IN_WINDOWS) {
        ret = mouseWindowHook(hwnd_, input_type_);
    }

    if (ret) {
        InputHook::input_hwnd = hwnd_;
        InputHook::input_type = input_type_;
    }

    return ret ? 1 : -1;

}

int InputHook::release()
{
    LONG_PTR ptr = 0;
    if (InputHook::input_type == INPUT_TYPE::IN_DX || InputHook::input_type == INPUT_TYPE::IN_WINDOWS) {
        MH_RemoveHook(NULL);
        MH_Uninitialize();
    }

    if (gRawWindowProc)
    {
        ptr = SetWindowLongPtrA(InputHook::input_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(gRawWindowProc));
    }
    return ptr ? 1 : 0;
    return 0;
}

void InputHook::upDataPos(LPARAM lp, int key, bool down)
{
    m_mouseState.lAxisX = lp & 0xffff;
    m_mouseState.lAxisY = (lp >> 16) & 0xffff;
    setlog("upDataPos x=%d, y=%d", m_mouseState.lAxisX, m_mouseState.lAxisY);
    if (0 <= key && key < 3)
    {
        m_mouseState.abButtons[key] = down ? 0x80 : 0;
    }
}

bool InputHook::mouseDxHook(HWND hwnd, int input_type)
{
    if (getDinputVtb() == 1)
    {
        MH_Initialize();
        MH_CreateHook(
            gDinputVtb[indexGetDeviceState],
            hkGetDeviceState,
            &gDinputVtbRaw[indexGetDeviceState]);
        MH_CreateHook(
            gDinputVtb[indexPoll],
            hkPoll,
            &gDinputVtbRaw[indexPoll]);
        MH_EnableHook(NULL);
        gRawWindowProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(opWndProc)));
    }
    else
    {
        setlog("getDinputVtb false!");
    }

    return gRawWindowProc ? 1 : -1;
}

bool InputHook::mouseWindowHook(HWND hwnd, int input_type)
{
    MH_Initialize();
    MH_CreateHook(&GetMessage, hkGetMessage, &rawGetMessage);
    return MH_EnableHook(NULL) == MH_OK;
}

int getDinputVtb()
{
    using DirectInput8Create_t = decltype(DirectInput8Create);
    auto pDirectInput8Create = reinterpret_cast<DirectInput8Create_t *>(query_api("dinput8.dll", "DirectInput8Create"));
    if (pDirectInput8Create)
    {
        WNDCLASSEX windowClass;
        windowClass.cbSize = sizeof(WNDCLASSEX);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = DefWindowProc;
        windowClass.cbClsExtra = 0;
        windowClass.cbWndExtra = 0;
        windowClass.hInstance = GetModuleHandle(NULL);
        windowClass.hIcon = NULL;
        windowClass.hCursor = NULL;
        windowClass.hbrBackground = NULL;
        windowClass.lpszMenuName = NULL;
        windowClass.lpszClassName = L"Kiero";
        windowClass.hIconSm = NULL;

        ::RegisterClassEx(&windowClass);

        HWND window = ::CreateWindowW(windowClass.lpszClassName, L"Kiero DirectX Window", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, windowClass.hInstance, NULL);
        LPDIRECTINPUT8 pDinput = NULL;

        if (pDirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION,
                                OP_IID_IDirectInput8W, (VOID **)&pDinput, NULL) < 0)
        {
            ::DestroyWindow(window);
            ::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
            setlog("pDirectInput8Create false!");
            return -1;
        }

        LPDIRECTINPUTDEVICE8 pMouse = NULL;
        if (pDinput->CreateDevice(OP_GUID_SysMouse, &pMouse, NULL) < 0)
        {
            ::DestroyWindow(window);
            ::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
            setlog("CreateDevice false!");
            return -2;
        }
        gDinputVtb.resize(32);
        gDinputVtbRaw.resize(32);
        ::memcpy(&gDinputVtb[0], *(size_t **)pMouse, 32 * sizeof(size_t));

        ::DestroyWindow(window);
        ::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);

        return 1;
        //MH_Initialize();
    }
    else
    {
        setlog("dinput8 not found");
        return 0;
    }
}

HRESULT __stdcall hkAcquire(IDirectInputDevice8W *this_)
{
    return DI_OK;
}

HRESULT __stdcall hkPoll(IDirectInputDevice8W *this_)
{
    return DI_OK;
}

BOOL __stdcall hkGetMessage(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
    using GetMessage_t = decltype(hkGetMessage);
    BOOL ret = reinterpret_cast<GetMessage_t*>(rawGetMessage)(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);

    UINT message = lpMsg->message;

    if (message == OP_WM_MOUSEMOVE || message == OP_WM_LBUTTONUP || message == OP_WM_LBUTTONDOWN ||
        message == OP_WM_MBUTTONDOWN || message == OP_WM_MBUTTONUP || message == OP_WM_RBUTTONDOWN ||
        message == OP_WM_RBUTTONUP || message == OP_WM_MOUSEWHEEL) {
        lpMsg->message = message - WM_USER;
    }

    if (InputHook::is_lock_mouse) {
        if (isMouseMessage(message)) {
            do {
                ret = reinterpret_cast<GetMessage_t*>(rawGetMessage)(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
                message = lpMsg->message;
            } while (ret && isMouseMessage(message));
        }
    }
    return ret;
}

bool isMouseMessage(UINT message) {
    return message == WM_MOUSEMOVE || message == WM_LBUTTONUP || message == WM_LBUTTONDOWN ||
        message == WM_MBUTTONDOWN || message == WM_MBUTTONUP || message == WM_RBUTTONDOWN ||
        message == WM_RBUTTONUP || message == WM_MOUSEWHEEL || message == WM_LBUTTONDBLCLK ||
        message == WM_RBUTTONDBLCLK || message == WM_MBUTTONDBLCLK;
}

void EndianSwap(char *pData, int startIndex, int length)
{
    int i,cnt,end,start;
    cnt = length / 2;
    start = startIndex;
    end  = startIndex + length - 1;
    char tmp;
    for (i = 0; i < cnt; i++)
    {
        tmp            = pData[start+i];
        pData[start+i] = pData[end-i];
        pData[end-i]   = tmp;
    }
}

HRESULT __stdcall hkGetDeviceState(IDirectInputDevice8W *this_, DWORD size, LPVOID ptr)
{
    //setlog("called hkGetDeviceState");
    using GetDeviceState_t = decltype(hkGetDeviceState);

    if (size == sizeof(opMouseState))
    {
        opMouseState state = {};
        //setlog("called opMouseState");
        // state.lAxisX = InputHook::m_mouseState.lAxisX;
        // state.lAxisY = InputHook::m_mouseState.lAxisY;
        // state.abButtons[0]
        state = InputHook::m_mouseState;
        //EndianSwap((char*)&state,0, sizeof(state));
        memcpy(ptr, &state, sizeof(state));
        return DI_OK;
    }
    else if (size == sizeof(DIMOUSESTATE))
    {
        DIMOUSESTATE state = {};
        setlog("called DIMOUSESTATE");

        state.lX = InputHook::m_mouseState.lAxisX;
        state.lY = InputHook::m_mouseState.lAxisY;

        memcpy(ptr, &state, sizeof(state));
        return DI_OK;
    }
    else if (size == sizeof(DIMOUSESTATE2))
    {
        DIMOUSESTATE2 state = {};
        setlog("called DIMOUSESTATE2");
        state.lX = InputHook::m_mouseState.lAxisX;
        state.lY = InputHook::m_mouseState.lAxisY;
        memcpy(ptr, &state, sizeof(state));
        return DI_OK;
    }
    else
    {
        return reinterpret_cast<GetDeviceState_t *>(gDinputVtbRaw[indexGetDeviceState])(this_, size, ptr);
    }
}

LRESULT CALLBACK opWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    setlog("%04X message", message);
    switch (message)
    {
    case OP_WM_MOUSEMOVE:
    {
        InputHook::upDataPos(lParam, -1, false);
        setlog("OP_WM_MOUSEMOVE message");
        break;
    }
    case OP_WM_LBUTTONUP:
        InputHook::upDataPos(lParam, 0, false);
        setlog("OP_WM_LBUTTONUP message");
        break;
    case OP_WM_LBUTTONDOWN:
        InputHook::upDataPos(lParam, 0, true);
        setlog("OP_WM_LBUTTONDOWN message");
        break;
    case OP_WM_MBUTTONDOWN:
        InputHook::upDataPos(lParam, 1, true);
        setlog("OP_WM_MBUTTONDOWN message");
        break;
    case OP_WM_MBUTTONUP:
        InputHook::upDataPos(lParam, 1, false);
        setlog("OP_WM_MBUTTONUP message");
        break;
    case OP_WM_RBUTTONDOWN:
        InputHook::upDataPos(lParam, 2, true);
        setlog("OP_WM_RBUTTONDOWN message");
        break;
    case OP_WM_RBUTTONUP:
        InputHook::upDataPos(lParam, 2, false);
        setlog("OP_WM_RBUTTONUP message");
        break;
    case OP_WM_MOUSEWHEEL:
        //InputHook::upDataPos(wParam,2,true);
        setlog("OP_WM_MOUSEWHEEL message");
        break;
    }
    return CallWindowProcA(gRawWindowProc, InputHook::input_hwnd, message, wParam, lParam);
}
