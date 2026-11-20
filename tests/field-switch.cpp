#include <windows.h>
#include <filesystem>
#include <iostream>

HWND first,second;
void pump(DWORD ms) {
    auto until=GetTickCount64()+ms;
    do{
        MSG m{};
        while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){
            if(m.message==WM_KEYDOWN && m.wParam==VK_TAB && GetFocus()==first){SetFocus(second);continue;}
            TranslateMessage(&m);DispatchMessageW(&m);
        }
        Sleep(5);
    }while(GetTickCount64()<until);
}
void key(WORD vk,bool up=false) {
    INPUT i{};i.type=INPUT_KEYBOARD;i.ki.wVk=vk;i.ki.dwFlags=up?KEYEVENTF_KEYUP:0;SendInput(1,&i,sizeof(i));
}
int wmain(int argc,wchar_t **argv) {
    if(argc!=2)return 1;
    WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpfnWndProc=DefWindowProcW;c.lpszClassName=L"QuietType.TwoFields";
    RegisterClassW(&c);
    HWND h=CreateWindowExW(WS_EX_TOPMOST,c.lpszClassName,L"QuietType-acceptance-TwoFields",WS_OVERLAPPEDWINDOW,0,0,400,250,nullptr,nullptr,c.hInstance,nullptr);
    first=CreateWindowW(L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER,10,15,320,40,h,nullptr,c.hInstance,nullptr);
    second=CreateWindowW(L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER,10,75,320,40,h,nullptr,c.hInstance,nullptr);
    ShowWindow(h,SW_SHOW);
    DWORD me=GetCurrentThreadId(), fg=GetWindowThreadProcessId(GetForegroundWindow(),nullptr);
    bool attached=me!=fg && AttachThreadInput(me,fg,TRUE);
    BringWindowToTop(h);SetForegroundWindow(h);SetActiveWindow(h);SetFocus(first);
    if(attached)AttachThreadInput(me,fg,FALSE);
    pump(200);
    if(GetForegroundWindow()!=h)return 2;
    HWND overlay=FindWindowW(L"QuietType.Status",nullptr);
    key(VK_CONTROL);key(VK_SPACE);pump(700);bool listened=IsWindowVisible(overlay);
    key(VK_ESCAPE);key(VK_ESCAPE,true);for(int i=0;i<80 && IsWindowVisible(overlay);++i)pump(30);bool stopped=!IsWindowVisible(overlay);
    key(VK_SPACE,true);key(VK_CONTROL,true);
    std::cout<<"Installed hold: "<<listened<<"; Escape while held: "<<stopped<<std::endl;
    if(!listened||!stopped)return 3;
    auto helper=std::filesystem::absolute(L".build/desktop-probe.exe");
    std::wstring cmd=L"\""+helper.wstring()+L"\" \""+std::wstring(argv[1])+L"\" QuietType-acceptance-TwoFields tab";
    STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    if(!CreateProcessW(helper.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return 4;
    CloseHandle(pi.hThread);
    while(WaitForSingleObject(pi.hProcess,0)==WAIT_TIMEOUT)pump(10);
    DWORD code;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hProcess);
    if(code||GetFocus()!=second||GetWindowTextLengthW(first)||GetWindowTextLengthW(second))return 5;
    std::cout<<"Passed same-window focus switch. Neither test field received a paste.\n";
    return 0;
}
