// Exercise the production pipeline on a known scratch field with repeatable audio.
#include "../src/main.cpp"
#include <fstream>
#include <iostream>
#include <psapi.h>

bool pump(int milliseconds) {
    ULONGLONG until=GetTickCount64()+milliseconds;
    MSG m{};
    do {
        while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){
            if(m.message==WM_QUIT)return false;
            TranslateMessage(&m);DispatchMessageW(&m);
        }
        Sleep(8);
    }while(GetTickCount64()<until);
    return true;
}
std::wstring textValue(IUIAutomationElement *el) {
    BSTR value=nullptr; IUIAutomationValuePattern *vp=nullptr;
    if(SUCCEEDED(el->GetCurrentPatternAs(UIA_ValuePatternId,__uuidof(IUIAutomationValuePattern),(void**)&vp))&&vp){
        vp->get_CurrentValue(&value);vp->Release();
    }
    if(!value || !*value){
        SysFreeString(value);value=nullptr;
        IUIAutomationTextPattern *tp=nullptr;IUIAutomationTextRange *range=nullptr;
        if(SUCCEEDED(el->GetCurrentPatternAs(UIA_TextPatternId,__uuidof(IUIAutomationTextPattern),(void**)&tp))&&tp){
            if(SUCCEEDED(tp->get_DocumentRange(&range))&&range){range->GetText(32000,&value);range->Release();}
            tp->Release();
        }
    }
    std::wstring text=value?value:L"";SysFreeString(value);return text;
}
size_t workingSet(HANDLE process) {
    PROCESS_MEMORY_COUNTERS counters{sizeof(counters)};
    return GetProcessMemoryInfo(process,&counters,sizeof(counters)) ? counters.WorkingSetSize : 0;
}
size_t combinedWorkingSet() {
    size_t total=workingSet(GetCurrentProcess());
    JOBOBJECT_BASIC_PROCESS_ID_LIST list{};
    if (QueryInformationJobObject(job.h,JobObjectBasicProcessIdList,&list,sizeof(list),nullptr) &&
            list.NumberOfProcessIdsInList==1) {
        Handle child(OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,list.ProcessIdList[0]));
        if(child.h) total+=workingSet(child.h);
    }
    return total;
}
int wmain(int argc,wchar_t **argv) {
    if(argc!=4)return 2;
    instance=GetModuleHandleW(nullptr);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(CoCreateInstance(__uuidof(CUIAutomation),nullptr,CLSCTX_INPROC_SERVER,__uuidof(IUIAutomation),(void**)&automation)))return 3;
    auto root=fs::current_path();exe=root/L"dist/app/QuietType.exe";models=root/L".build/models";
    WNDCLASSW wc{};wc.hInstance=instance;wc.lpszClassName=L"QuietType.Probe";wc.lpfnWndProc=dispatch;RegisterClassW(&wc);
    window=CreateWindowW(wc.lpszClassName,L"",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
    indicator=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"",WS_POPUP,0,0,20,20,nullptr,nullptr,instance,nullptr);
    job.h=CreateJobObjectW(nullptr,nullptr);cancelled.h=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::ifstream file(fs::path(argv[1]),std::ios::binary);
    std::string audio(std::istreambuf_iterator<char>(file),{});
    if(audio.empty())return 4;
    auto deadline=GetTickCount64()+25000;
    while(GetTickCount64()<deadline){
        HWND fg=GetForegroundWindow();wchar_t cap[512]{};GetWindowTextW(fg,cap,512);
        DWORD pid=0;GetWindowThreadProcessId(fg,&pid);
        if((wcsstr(cap,argv[2]) && wcsstr(cap,L"QuietType-acceptance-")) || (argv[2][0] == L'#' && pid==wcstoul(argv[2]+1,nullptr,10))){
            target=fg;nativeEditor=scintillaFocus(target);automation->GetFocusedElement(&field);
            if(field && sameField())break;
            if(field){field->Release();field=nullptr;}
        }
        pump(100);
    }
    if(!field){std::cerr<<"Could not focus the named scratch editor.\n";return 5;}
    auto editorHandle=nativeEditor;
    auto original=textValue(field);
    field->AddRef();auto observed=field;
    focusHook=SetWinEventHook(EVENT_OBJECT_FOCUS,EVENT_OBJECT_FOCUS,nullptr,focusEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    windowHook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,focusEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job.h,JobObjectExtendedLimitInformation,&limits,sizeof(limits));
    SetTimer(window,1,20,nullptr);
    size_t peak=0;
    auto begin=GetTickCount64();
    processRecording(std::move(audio));
    if(wcscmp(argv[3],L"cancel")==0)stopWork();
    if(wcscmp(argv[3],L"switch")==0){
        auto other=CreateWindowW(L"EDIT",L"QuietType test alternate field",WS_OVERLAPPEDWINDOW,0,0,200,100,nullptr,nullptr,instance,nullptr);
        ShowWindow(other,SW_SHOW);SetForegroundWindow(other);SetFocus(other);
    }
    if(wcscmp(argv[3],L"tab")==0) {
        INPUT keys[2]{};keys[0].type=keys[1].type=INPUT_KEYBOARD;
        keys[0].ki.wVk=keys[1].ki.wVk=VK_TAB;keys[1].ki.dwFlags=KEYEVENTF_KEYUP;
        SendInput(2,keys,sizeof(INPUT));
    }
    while(state!=State::Idle && GetTickCount64()-begin<90000){pump(10);peak=std::max(peak,combinedWorkingSet());}
    if(state!=State::Idle){stopWork();while(pipeline.joinable())pump(20);return 6;}
    auto elapsed=GetTickCount64()-begin;pump(350);
    auto contents=textValue(observed);observed->Release();
    if(editorHandle) {
        wchar_t buffer[4096]{};
        SendMessageTimeoutW(editorHandle,WM_GETTEXT,4096,(LPARAM)buffer,SMTO_ABORTIFHUNG,500,nullptr);
        contents=buffer;
    }
    std::wcout<<L"Time "<<elapsed<<L" ms. Peak app+worker "<<peak/(1024*1024)<<L" MiB. Idle "<<workingSet(GetCurrentProcess())/(1024*1024)<<L" MiB. Edited output: "<<last<<L"\n";
    if(wcscmp(argv[3],L"cancel")==0)return last.empty()&&contents==original?0:7;
    if(wcscmp(argv[3],L"switch")==0 || wcscmp(argv[3],L"tab")==0)return !last.empty()&&contents==original?0:8;
    if(!last.empty() && contents!=original && contents.find(last)!=std::wstring::npos)return 0;
    std::cerr<<"Could not confirm inserted text in scratch field.\n";return 9;
}
