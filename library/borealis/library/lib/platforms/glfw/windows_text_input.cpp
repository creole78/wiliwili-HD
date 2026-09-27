// Native Windows edit control: TSF/IME composition, candidates and touch keyboard.
#if defined(_WIN32) && !defined(__WINRT__)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>
#include <borealis/core/application.hpp>
#include <borealis/core/thread.hpp>
#include <functional>
#include <string>
#include <vector>

namespace brls {
constexpr COLORREF nativeRGB(unsigned r, unsigned g, unsigned b) { return r | (g << 8) | (b << 16); }
// Documented InputPane COM ABI. Kept local to allow older SDK build targets.
struct PaneInterop : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetForWindow(HWND, REFIID, void**) = 0;
};
struct Pane2 : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE TryShow(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryHide(boolean*) = 0;
};
namespace {
const IID paneInteropId = {0x75cf2c57,0x9195,0x4931,{0x83,0x32,0xf0,0xb4,0x09,0xe9,0x16,0xaf}};
const IID pane2Id = {0x8a6b3f26,0x7090,0x4793,{0x94,0x4c,0xc3,0xf2,0xcd,0xe2,0x62,0x76}};
HWND activeTextWindow = nullptr;

std::wstring toWide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    std::wstring result(n, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), &result[0], n);
    return result;
}
std::string toUtf8(const std::wstring& s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string result(n, '\0');
    if (n) WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &result[0], n, nullptr, nullptr);
    return result;
}
struct TextWindow {
    HWND owner = nullptr, edit = nullptr;
    HFONT font = nullptr;
    HBRUSH background = nullptr;
    COLORREF textColor = nativeRGB(32,32,32);
    COLORREF backgroundColor = nativeRGB(250,250,250);
    WNDPROC editProc = nullptr;
    Pane2* pane = nullptr;
    bool ownsLifetime = false;
    HMODULE combase = nullptr;
    bool uninitialize = false, composing = false;
    std::function<void(std::string)> submit;
};
void showKeyboard(TextWindow* data) {
    // InputPane may report success or zero occlusion before its slide-in finishes.
    // Never replace the tablet keyboard with OSK based on these transient values.
    boolean shown = false;
    if (data->pane) data->pane->TryShow(&shown);
}
void initializePane(HWND window, TextWindow* data) {
    data->combase = LoadLibraryW(L"combase.dll");
    if (!data->combase) return;
    auto init = reinterpret_cast<decltype(&RoInitialize)>(GetProcAddress(data->combase, "RoInitialize"));
    auto create = reinterpret_cast<decltype(&WindowsCreateString)>(GetProcAddress(data->combase, "WindowsCreateString"));
    auto destroy = reinterpret_cast<decltype(&WindowsDeleteString)>(GetProcAddress(data->combase, "WindowsDeleteString"));
    auto factory = reinterpret_cast<decltype(&RoGetActivationFactory)>(GetProcAddress(data->combase, "RoGetActivationFactory"));
    if (!init || !create || !destroy || !factory) return;
    HRESULT hr = init(RO_INIT_SINGLETHREADED);
    data->uninitialize = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return;
    const wchar_t name[] = L"Windows.UI.ViewManagement.InputPane";
    HSTRING str = nullptr;
    if (FAILED(create(name, (UINT32)(std::size(name)-1), &str))) return;
    PaneInterop* interop = nullptr;
    if (SUCCEEDED(factory(str, paneInteropId, reinterpret_cast<void**>(&interop)))) {
        interop->GetForWindow(window, pane2Id, reinterpret_cast<void**>(&data->pane));
        interop->Release();
    }
    destroy(str);
}
LRESULT CALLBACK editProcedure(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* data = reinterpret_cast<TextWindow*>(GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA));
    if (!data) return DefWindowProcW(hwnd,msg,wp,lp);
    if (msg == WM_IME_STARTCOMPOSITION) data->composing = true;
    if (msg == WM_IME_ENDCOMPOSITION) data->composing = false;
    if (msg == WM_KEYDOWN && !data->composing && (wp == VK_RETURN || wp == VK_ESCAPE)) {
        PostMessageW(GetParent(hwnd), WM_COMMAND, wp == VK_RETURN ? IDOK : IDCANCEL, 0);
        return 0;
    }
    if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;
    return CallWindowProcW(data->editProc,hwnd,msg,wp,lp);
}
void closeTextWindow(HWND hwnd, TextWindow* data) {
    // Enable the owner before destroying the active dialog. Otherwise Windows
    // may briefly activate another application while the owner is still disabled.
    HWND owner = data->owner;
    const bool wasActive = GetActiveWindow() == hwnd;
    KillTimer(hwnd,1);
    EnableWindow(owner,TRUE);
    DestroyWindow(hwnd);
    if (wasActive && IsWindow(owner)) {
        SetActiveWindow(owner);
        SetFocus(owner);
    }
    Application::setActiveEvent(true);
}
LRESULT CALLBACK textProcedure(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* data = reinterpret_cast<TextWindow*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        data = reinterpret_cast<TextWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(data));
    }
    if (!data) return DefWindowProcW(hwnd,msg,wp,lp);
    switch (msg) {
        case WM_TIMER:
            KillTimer(hwnd,wp);
            if (wp == 1) showKeyboard(data);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == 1003) { SetFocus(data->edit); showKeyboard(data); return 0; }
            if (LOWORD(wp) == IDOK) {
                if (data->composing) { SetFocus(data->edit); return 0; }
                std::vector<wchar_t> buffer(GetWindowTextLengthW(data->edit)+1);
                GetWindowTextW(data->edit,buffer.data(),(int)buffer.size());
                auto value = toUtf8(buffer.data());
                auto callback = data->submit;
                closeTextWindow(hwnd,data);
                brls::sync([callback,value]() { callback(value); });
                return 0;
            }
            if (LOWORD(wp) == IDCANCEL) { closeTextWindow(hwnd,data); return 0; }
            break;
        case WM_CLOSE: closeTextWindow(hwnd,data); return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
            SetTextColor(reinterpret_cast<HDC>(wp),data->textColor);
            SetBkColor(reinterpret_cast<HDC>(wp),data->backgroundColor);
            return reinterpret_cast<LRESULT>(data->background);
        case WM_ERASEBKGND: {
            RECT rect; GetClientRect(hwnd,&rect);
            FillRect(reinterpret_cast<HDC>(wp),&rect,data->background);
            return 1;
        }
        case WM_NCDESTROY:
            KillTimer(hwnd,1);
            if (data->pane) { boolean hidden; data->pane->TryHide(&hidden); data->pane->Release(); }
            if (data->uninitialize) {
                auto uninit = reinterpret_cast<decltype(&RoUninitialize)>(GetProcAddress(data->combase,"RoUninitialize"));
                if (uninit) uninit();
            }
            if (data->combase) FreeLibrary(data->combase);
            EnableWindow(data->owner,TRUE);
            if (data->font) DeleteObject(data->font);
            if (data->ownsLifetime && data->background) DeleteObject(data->background);
            activeTextWindow = nullptr;
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);
            if (data->ownsLifetime) delete data;
            return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}
bool openWindowsTextInput(GLFWwindow* window, std::function<void(std::string)> callback,
    const std::string& title, const std::string& hint, size_t maxLength, const std::string& initial) {
    if (activeTextWindow) { SetForegroundWindow(activeTextWindow); return true; }
    HINSTANCE instance = GetModuleHandleW(nullptr);
    static const wchar_t className[] = L"WiliwiliTextInput";
    WNDCLASSW wc{}; wc.lpfnWndProc = textProcedure; wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr,MAKEINTRESOURCEW(32513)); wc.lpszClassName = className;
    RegisterClassW(&wc);
    auto* data = new TextWindow();
    data->owner = glfwGetWin32Window(window);
    data->submit = std::move(callback);
    auto background = Application::getTheme().getColor("brls/background");
    auto foreground = Application::getTheme().getColor("brls/text");
    data->backgroundColor = nativeRGB(background.r*255,background.g*255,background.b*255);
    data->textColor = nativeRGB(foreground.r*255,foreground.g*255,foreground.b*255);
    data->background = CreateSolidBrush(data->backgroundColor);
    RECT owner; GetWindowRect(data->owner,&owner);
    HDC dc = GetDC(data->owner);
    int dpi = GetDeviceCaps(dc,LOGPIXELSY); ReleaseDC(data->owner,dc);
    auto px = [dpi](int v) { return MulDiv(v,dpi,96); };
    int width = std::min(px(640),(int)(owner.right-owner.left));
    int height = px(225);
    MONITORINFO monitor{sizeof(MONITORINFO)};
    GetMonitorInfoW(MonitorFromWindow(data->owner,MONITOR_DEFAULTTONEAREST), &monitor);
    int left = std::max((int)monitor.rcWork.left, (int)std::min(owner.left + ((owner.right-owner.left)-width)/2, monitor.rcWork.right-width));
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME,className,toWide(title).c_str(),
        WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN,left,
        monitor.rcWork.top+px(24),width,height,data->owner,nullptr,instance,data);
    if (!hwnd) { DeleteObject(data->background); delete data; return false; }
    data->ownsLifetime = true;
    activeTextWindow = hwnd;
    data->font = CreateFontW(-px(20),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    auto control = [&](DWORD ex, const wchar_t* cls, const std::wstring& text, DWORD style,
        int x,int y,int w,int h,int id) {
        HWND c = CreateWindowExW(ex,cls,text.c_str(),WS_CHILD|WS_VISIBLE|style,
            x,y,w,h,hwnd,reinterpret_cast<HMENU>((INT_PTR)id),instance,nullptr);
        SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(data->font),TRUE);
        return c;
    };
    bool zh = Application::getLocale().rfind("zh",0)==0;
    control(0,L"STATIC",toWide(hint.empty() ? (zh ? "使用系统输入法输入拼音并选择汉字" : "Use your system keyboard and input method") : hint),0,px(18),px(14),width-px(40),px(30),1001);
    data->edit = control(WS_EX_CLIENTEDGE,L"EDIT",toWide(initial),WS_TABSTOP|ES_AUTOHSCROLL,
        px(18),px(53),width-px(40),px(40),1002);
    SendMessageW(data->edit,EM_SETLIMITTEXT,maxLength,0);
    SendMessageW(data->edit,EM_SETSEL,(WPARAM)-1,(LPARAM)-1);
    data->editProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(data->edit,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(editProcedure)));
    control(0,L"BUTTON",zh?L"触摸键盘":L"Touch keyboard",WS_TABSTOP,px(18),px(117),px(145),px(42),1003);
    control(0,L"BUTTON",zh?L"取消":L"Cancel",WS_TABSTOP,width-px(250),px(117),px(105),px(42),IDCANCEL);
    control(0,L"BUTTON",zh?L"确定":L"OK",WS_TABSTOP|BS_DEFPUSHBUTTON,width-px(130),px(117),px(105),px(42),IDOK);
    // Transfer activation directly to the owned dialog before disabling its owner.
    ShowWindow(hwnd,SW_SHOW);
    SetFocus(data->edit);
    EnableWindow(data->owner,FALSE);
    Application::setActiveEvent(true);
    initializePane(hwnd,data);
    SetTimer(hwnd,1,200,nullptr);
    return true;
}
} // namespace brls
#endif
