#include "Window.hpp"
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <Windows.h>
#include <map>

namespace WindowGlobal
{
    bool isOpen = false;
    const int eventStackSize = 10;
    ae3d::WindowEvent eventStack[eventStackSize];
    int eventIndex = -1;
    HWND hwnd;
    HDC hdc;
    std::map< unsigned, ae3d::KeyCode > keyMap = {
        {65, ae3d::KeyCode::A},
        {66, ae3d::KeyCode::B},
        {67, ae3d::KeyCode::C},
        {68, ae3d::KeyCode::D},
        {69, ae3d::KeyCode::E},
        {70, ae3d::KeyCode::F},
        {71, ae3d::KeyCode::G},
        {72, ae3d::KeyCode::H},
        {73, ae3d::KeyCode::I},
        {74, ae3d::KeyCode::J},
        {75, ae3d::KeyCode::K},
        {76, ae3d::KeyCode::L},
        {77, ae3d::KeyCode::M},
        {78, ae3d::KeyCode::N},
        {79, ae3d::KeyCode::O},
        {80, ae3d::KeyCode::P},
        {81, ae3d::KeyCode::Q},
        {82, ae3d::KeyCode::R},
        {83, ae3d::KeyCode::S},
        {84, ae3d::KeyCode::T},
        {85, ae3d::KeyCode::U},
        {86, ae3d::KeyCode::V},
        {87, ae3d::KeyCode::W},
        {88, ae3d::KeyCode::X},
        {89, ae3d::KeyCode::Y},
        {90, ae3d::KeyCode::Z},
        {38, ae3d::KeyCode::Up},
        {40, ae3d::KeyCode::Down},
        {37, ae3d::KeyCode::Left},
        {39, ae3d::KeyCode::Right},
        {27, ae3d::KeyCode::Escape},
        {32, ae3d::KeyCode::Space},
        {13, ae3d::KeyCode::Enter}
    };
}

static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_ACTIVATEAPP:
            //RendererImpl::Instance().SetFocus((wParam == WA_INACTIVE) ? false : true);
            break;
        case WM_SIZE:
        case WM_SETCURSOR:
        case WM_DESTROY:
        case WM_SYSKEYUP:
        case WM_KEYUP:
            break;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        {
            ++WindowGlobal::eventIndex;
            WindowGlobal::eventStack[WindowGlobal::eventIndex].type = ae3d::WindowEventType::KeyDown;
            //ae3d::System::Print("Got key down: %d", wParam);
            WindowGlobal::eventStack[WindowGlobal::eventIndex].keyCode = WindowGlobal::keyMap[ (unsigned)wParam ];
        }
        break;
        case WM_SYSCOMMAND:
            if (wParam == SC_MINIMIZE)
            {
                // Handled by WM_SIZE.
            }
            else if (wParam == SC_RESTORE)
            {
            }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        {
            ++WindowGlobal::eventIndex;
            WindowGlobal::eventStack[WindowGlobal::eventIndex].type = message == WM_LBUTTONDOWN ? ae3d::WindowEventType::Mouse1Down : ae3d::WindowEventType::Mouse1Up;
            WindowGlobal::eventStack[WindowGlobal::eventIndex].mouseX = LOWORD(lParam);
            WindowGlobal::eventStack[WindowGlobal::eventIndex].mouseY = HIWORD(lParam);
        }
            break;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        {
            ++WindowGlobal::eventIndex;
            WindowGlobal::eventStack[WindowGlobal::eventIndex].type = message == WM_RBUTTONDOWN ? ae3d::WindowEventType::Mouse2Down : ae3d::WindowEventType::Mouse2Up;
            WindowGlobal::eventStack[WindowGlobal::eventIndex].mouseX = LOWORD(lParam);
            WindowGlobal::eventStack[WindowGlobal::eventIndex].mouseY = HIWORD(lParam);
        }
            break;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
            ++WindowGlobal::eventIndex;
            WindowGlobal::eventStack[WindowGlobal::eventIndex].type = message == WM_MBUTTONDOWN ? ae3d::WindowEventType::MouseMiddleDown : ae3d::WindowEventType::MouseMiddleUp;
            WindowGlobal::eventStack[WindowGlobal::eventIndex].mouseX = LOWORD(lParam);
            WindowGlobal::eventStack[WindowGlobal::eventIndex].mouseY = HIWORD(lParam);
            break;
        case WM_MOUSEWHEEL:
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            ++WindowGlobal::eventIndex;
            WindowGlobal::eventStack[WindowGlobal::eventIndex].type = delta < 0 ? ae3d::WindowEventType::MouseWheelScrollDown : ae3d::WindowEventType::MouseWheelScrollUp;
            WindowGlobal::eventStack[WindowGlobal::eventIndex].mouseX = LOWORD(lParam);
            WindowGlobal::eventStack[WindowGlobal::eventIndex].mouseY = HIWORD(lParam);
        }
            break;
        case WM_MOUSEMOVE:
            // Check to see if the left button is held down:
            //bool leftButtonDown=wParam & MK_LBUTTON;
            
            // Check if right button down:
            //bool rightButtonDown=wParam & MK_RBUTTON;
            break;
        case WM_CLOSE:
            ++WindowGlobal::eventIndex;
            WindowGlobal::eventStack[WindowGlobal::eventIndex].type = ae3d::WindowEventType::Close;
            break;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

namespace ae3d
{
    void Window::Create(int width, int height, WindowCreateFlags flags)
    {
        const int finalWidth = width == 0 ? GetSystemMetrics(SM_CXSCREEN) : width;
        const int finalHeight = height == 0 ? GetSystemMetrics(SM_CYSCREEN) : height;

        const HINSTANCE hInstance = GetModuleHandle(nullptr);
        const bool fullscreen = (flags & WindowCreateFlags::Fullscreen) != 0;

        WNDCLASSEX wc;
        ZeroMemory(&wc, sizeof(WNDCLASSEX));

        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
        wc.lpszClassName = "WindowClass1";

        wc.hIcon = (HICON)LoadImage(nullptr,
            "glider.ico",
            IMAGE_ICON,
            32,
            32,
            LR_LOADFROMFILE);

        RegisterClassEx(&wc);

        WindowGlobal::hwnd = CreateWindowExA(fullscreen ? WS_EX_TOOLWINDOW | WS_EX_TOPMOST : 0,
            "WindowClass1",    // name of the window class
            "Window",   // title of the window
            fullscreen ? WS_POPUP : (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU),    // window style
            CW_USEDEFAULT,    // x-position of the window
            CW_USEDEFAULT,    // y-position of the window
            finalWidth,    // width of the window
            finalHeight,    // height of the window
            nullptr,    // we have no parent window
            nullptr,    // we aren't using menus
            hInstance,    // application handle
            nullptr);    // used with multiple windows    
        
        ShowWindow(WindowGlobal::hwnd, SW_SHOW);
        CreateRenderer();
        WindowGlobal::isOpen = true;
    }

    bool Window::PollEvent(WindowEvent& outEvent)
    {
        if (WindowGlobal::eventIndex == -1)
        {
            return false;
        }

        outEvent = WindowGlobal::eventStack[WindowGlobal::eventIndex];
        --WindowGlobal::eventIndex;
        return true;
    }

    void Window::PumpEvents()
    {
        MSG msg;

        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    bool Window::IsOpen()
    {
        return WindowGlobal::isOpen;
    }

    void Window::SwapBuffers() const
    {
        ::SwapBuffers( WindowGlobal::hdc );
    }
}