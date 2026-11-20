#include "../src/main.cpp"
#include <iostream>
static void pump(int ms) {
    auto end=GetTickCount64()+ms;
    do {
        MSG m;
        while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}
        Sleep(5);
    }while(GetTickCount64()<end);
}
static void key(WORD vk,bool up=false) {
    INPUT i{};i.type=INPUT_KEYBOARD;i.ki.wVk=vk;i.ki.dwFlags=up?KEYEVENTF_KEYUP:0;
    SendInput(1,&i,sizeof(i));
}
int wmain() {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    instance=GetModuleHandleW(nullptr);models=fs::current_path()/L".build/models";
    exe=fs::current_path()/L"dist/app/QuietType.exe";
    WNDCLASSW c{};c.hInstance=instance;c.lpfnWndProc=dispatch;c.lpszClassName=L"QuietType.ControlTest";
    RegisterClassW(&c);window=CreateWindowW(c.lpszClassName,L"QuietType shortcut test",WS_OVERLAPPEDWINDOW,20,20,320,140,0,0,instance,0);
    HWND edit=CreateWindowW(L"EDIT",L"",WS_CHILD|WS_VISIBLE,0,0,260,50,window,0,instance,0);
    ShowWindow(window,SW_SHOW);SetForegroundWindow(window);SetFocus(edit);
    indicator=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"",WS_POPUP,0,0,40,20,nullptr,nullptr,instance,nullptr);
    cancelled.h=CreateEventW(nullptr,TRUE,FALSE,nullptr);job.h=CreateJobObjectW(nullptr,nullptr);
    if(!ready() || !bind(MAKEWORD(VK_F8,HOTKEYF_CONTROL)))return 1;
    WORD original=chord;
    if(!RegisterHotKey(edit,7,MOD_CONTROL,VK_F9))return 2;
    if(bind(MAKEWORD(VK_F9,HOTKEYF_CONTROL))||chord!=original)return 3;
    UnregisterHotKey(edit,7);
    if(!bind(MAKEWORD(VK_F10,HOTKEYF_CONTROL)))return 4;
    auto down=[](){key(VK_CONTROL);key(VK_F10);pump(180);};
    auto up=[](){key(VK_F10,true);key(VK_CONTROL,true);pump(80);};
    down();bool listening=state==State::Listening;up();
    if(!listening || state==State::Listening){std::cerr<<"Hold or microphone open failed\n";return 5;}
    stopWork();while(pipeline.joinable())pump(10);
    down();key(VK_ESCAPE);key(VK_ESCAPE,true);pump(80);bool cancelledWhileHeld=state==State::Idle;up();
    if(!cancelledWhileHeld)return 11;
    toggle=true;down();up();if(state!=State::Listening)return 6;
    down();up();if(state==State::Listening)return 7;
    stopWork();while(pipeline.joinable())pump(10);
    down();up();key(VK_ESCAPE);key(VK_ESCAPE,true);pump(80);
    if(state!=State::Idle)return 8;
    microphone=L"Missing microphone for acceptance test";down();up();if(state!=State::Idle)return 9;
    processRecording({});if(state!=State::Idle||pipeline.joinable())return 10;
    std::cout<<"PASS: binding conflict, rebind, microphone open/close, hold release, toggle, Escape, missing device and empty recording.\n";
    return 0;
}
