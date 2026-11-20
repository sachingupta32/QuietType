#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uiautomation.h>
#include <thread>
#include <memory>
#include <algorithm>
#include "visual.h"
#include "audio.h"
#include "process.h"

namespace fs=std::filesystem;
enum class State { Idle, Listening, Transcribing, Polishing, Delivering };
constexpr UINT Tray=WM_APP+1,Polishing=WM_APP+2,Finished=WM_APP+3,FirstRun=WM_APP+4,CancelDictation=WM_APP+5;
constexpr wchar_t Speech[]=L"ggml-base.en-q5_1.bin", Language[]=L"qwen2.5-1.5b-instruct-q4_k_m.gguf";
HWND window,indicator,dialog;
HINSTANCE instance;
fs::path exe,folder,models,ini;
NOTIFYICONDATAW icon{sizeof(icon)};
State state=State::Idle;
Recorder recorder;
WORD chord=MAKEWORD(VK_SPACE,HOTKEYF_CONTROL);
int binding=1;
bool toggle=false,registered=false,changedFocus=false,quitting=false;
std::wstring microphone,last,status;
HWND target, nativeEditor;
IUIAutomation *automation=nullptr;
IUIAutomationElement *field=nullptr;
HWINEVENTHOOK focusHook,windowHook;
HHOOK escapeHook;
ULONGLONG started;
Handle job,cancelled,setup;
std::thread pipeline;
struct Result { std::wstring text,message; bool insert=false; };

std::wstring wide(const std::string &s) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),s.size(),nullptr,0);
    std::wstring w(n,0); if(n) MultiByteToWideChar(CP_UTF8,0,s.data(),s.size(),w.data(),n);
    return w;
}
void notify(const std::wstring &message) {
    icon.uFlags=NIF_INFO; icon.dwInfoFlags=NIIF_INFO;
    lstrcpynW(icon.szInfoTitle,L"QuietType",64); lstrcpynW(icon.szInfo,message.c_str(),256);
    Shell_NotifyIconW(NIM_MODIFY,&icon);
}
void addIcon() {
    icon.hWnd=window; icon.uID=1; icon.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;
    icon.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(2)); icon.uCallbackMessage=Tray;
    lstrcpyW(icon.szTip,L"QuietType • Offline dictation"); Shell_NotifyIconW(NIM_ADD,&icon);
}
bool ready() {
    std::error_code ec;
    return fs::exists(models/L"verified-v1",ec) && fs::file_size(models/Speech,ec)==59721011
           && fs::file_size(models/Language,ec)==1117320736;
}
void setState(State next) {
    state=next;
    if(next==State::Idle) {ShowWindow(indicator,SW_HIDE);return;}
    status=next==State::Listening?L"Listening":next==State::Transcribing?L"Transcribing":next==State::Polishing?L"Polishing":L"Ready";
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(target,MONITOR_DEFAULTTONEAREST),&monitor);
    const auto &r=monitor.rcWork;
    const int dpi=visual::dpi(indicator);
    const int width=MulDiv(210,dpi,96),height=MulDiv(44,dpi,96);
    SetWindowRgn(indicator,CreateRoundRectRgn(0,0,width+1,height+1,height,height),FALSE);
    SetWindowPos(indicator,HWND_TOPMOST,(r.left+r.right-width)/2,r.bottom-height-MulDiv(18,dpi,96),
        width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    InvalidateRect(indicator,nullptr,FALSE);
}
LRESULT CALLBACK paint(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==WM_NCHITTEST) return HTTRANSPARENT;
    if(m==WM_PAINT) {
        PAINTSTRUCT ps{}; HDC dc=BeginPaint(h,&ps);RECT r{};GetClientRect(h,&r);
        int saved=SaveDC(dc);
        SetMapMode(dc,MM_ANISOTROPIC);SetWindowExtEx(dc,210,44,nullptr);
        SetViewportExtEx(dc,r.right,r.bottom,nullptr);
        bool hc=visual::highContrast();
        COLORREF bg=hc?GetSysColor(COLOR_WINDOW):visual::ink;
        COLORREF fg=hc?GetSysColor(COLOR_WINDOWTEXT):RGB(251,250,244);
        visual::fill(dc,{0,0,210,44},bg);
        int heights[]={7,15,23,13,7};
        for(int i=0;i<5;i++) {
            int x=17+4*i,top=22-heights[i]/2;
            visual::fill(dc,{x,top,x+2,top+heights[i]},hc?fg:visual::lime);
        }
        HFONT face=CreateFontW(-15,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        SelectObject(dc,face);SetTextColor(dc,fg);SetBkMode(dc,TRANSPARENT);
        RECT label{47,0,163,44};DrawTextW(dc,status.c_str(),-1,&label,DT_SINGLELINE|DT_VCENTER);
        RECT key{162,9,195,35};
        if(state!=State::Delivering){
            visual::fill(dc,key,hc?GetSysColor(COLOR_BTNFACE):RGB(59,54,76));
            SetTextColor(dc,hc?GetSysColor(COLOR_BTNTEXT):visual::lime);
            DrawTextW(dc,L"Esc",-1,&key,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
        RestoreDC(dc,saved);DeleteObject(face);EndPaint(h,&ps);return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
void CALLBACK focusEvent(HWINEVENTHOOK,DWORD event,HWND h,LONG,LONG,DWORD,DWORD) {
    if(state!=State::Idle && h && (event==EVENT_OBJECT_FOCUS || (event==EVENT_SYSTEM_FOREGROUND && h!=target))) changedFocus=true;
}
HWND scintillaFocus(HWND top) {
    GUITHREADINFO info{sizeof(info)};
    if (!GetGUIThreadInfo(GetWindowThreadProcessId(top,nullptr),&info) || !info.hwndFocus ||
        GetAncestor(info.hwndFocus,GA_ROOT)!=top) return nullptr;
    wchar_t name[64]{};
    GetClassNameW(info.hwndFocus,name,64);
    return wcscmp(name,L"Scintilla")==0 ? info.hwndFocus : nullptr;
}
void releaseTarget() {
    nativeEditor=nullptr;
    if(field){field->Release();field=nullptr;}
    if(focusHook) UnhookWinEvent(focusHook);
    if(windowHook) UnhookWinEvent(windowHook);
    focusHook=windowHook=nullptr;
}
bool sameField() {
    if(changedFocus || GetForegroundWindow()!=target) return false;
    if(nativeEditor) return scintillaFocus(target)==nativeEditor;
    if(!field || !automation) return false;
    IUIAutomationElement *now=nullptr; BOOL same=FALSE,secure=TRUE;
    if(SUCCEEDED(automation->GetFocusedElement(&now)) && now) {
        automation->CompareElements(field,now,&same);
        now->get_CurrentIsPassword(&secure);
        CONTROLTYPEID type=0; now->get_CurrentControlType(&type);
        if(type!=UIA_EditControlTypeId && type!=UIA_DocumentControlTypeId && type!=UIA_ComboBoxControlTypeId) same=FALSE;
        now->Release();
    }
    return same && !secure;
}
bool copyText(const std::wstring &text) {
    auto bytes=(text.size()+1)*sizeof(wchar_t);
    HGLOBAL data=GlobalAlloc(GMEM_MOVEABLE,bytes);
    if(!data)return false;
    auto ptr=GlobalLock(data);if(!ptr){GlobalFree(data);return false;}
    memcpy(ptr,text.c_str(),bytes);GlobalUnlock(data);
    bool ok=false;
    if(OpenClipboard(window)) {
        ok=EmptyClipboard() && SetClipboardData(CF_UNICODETEXT,data);
        if(ok) { // Opt out of Windows clipboard history and roaming for dictated text.
            for(auto name:{L"CanIncludeInClipboardHistory",L"CanUploadToCloudClipboard"}) {
                auto flag=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,sizeof(DWORD));
                if(flag && !SetClipboardData(RegisterClipboardFormatW(name),flag)) GlobalFree(flag);
            }
        }
        CloseClipboard();
    }
    if(!ok)GlobalFree(data);
    return ok;
}
bool paste() {
    if(!sameField() || !copyText(last) || !sameField())return false;
    INPUT keys[4]{};
    for(auto &k:keys)k.type=INPUT_KEYBOARD;
    keys[0].ki.wVk=keys[3].ki.wVk=VK_CONTROL;keys[1].ki.wVk=keys[2].ki.wVk='V';
    keys[2].ki.dwFlags=keys[3].ki.dwFlags=KEYEVENTF_KEYUP;
    return SendInput(4,keys,sizeof(INPUT))==4;
}
void idle() {
    KillTimer(window,1);
    if(escapeHook){UnhookWindowsHookEx(escapeHook);escapeHook=nullptr;}
    releaseTarget();setState(State::Idle);
}
void stopWork() {
    if(state==State::Listening){recorder.discard();idle();}
    else if(state==State::Delivering) idle();
    else if(pipeline.joinable()) {SetEvent(cancelled.h);TerminateJobObject(job.h,1);}
}
void setupModels() {
    if(setup.h){notify(L"Model setup is running.");return;}
    if(MessageBoxW(window,L"Download and verify the English speech and writing models? The first setup uses about 1.18 GB. Interrupted downloads can be resumed from the tray.",L"QuietType model setup",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
    wchar_t sys[MAX_PATH];GetSystemDirectoryW(sys,MAX_PATH);
    auto shell=fs::path(sys)/L"WindowsPowerShell/v1.0/powershell.exe";
    auto command=L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \""+(exe.parent_path()/L"models.ps1").wstring()+L"\"";
    STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    if(!CreateProcessW(shell.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){notify(L"Could not start model setup.");return;}
    setup.h=pi.hProcess;CloseHandle(pi.hThread);SetTimer(window,2,1000,nullptr);
    notify(L"Downloading and verifying models. QuietType will notify you when ready.");
}
std::wstring micName(UINT i) {
    WAVEINCAPSW caps{};
    return waveInGetDevCapsW(i,&caps,sizeof(caps))==0?caps.szPname:L"";
}
LRESULT CALLBACK onEscape(int code,WPARAM keyMessage,LPARAM data) {
    if(code==HC_ACTION && reinterpret_cast<KBDLLHOOKSTRUCT*>(data)->vkCode==VK_ESCAPE) {
        if(keyMessage==WM_KEYDOWN || keyMessage==WM_SYSKEYDOWN) PostMessageW(window,CancelDictation,0,0);
        return 1;
    }
    return CallNextHookEx(nullptr,code,keyMessage,data);
}
void startRecording() {
    if(!ready()){setupModels();return;}
    UINT mic=WAVE_MAPPER;
    if(!microphone.empty()){
        for(UINT i=0;i<waveInGetNumDevs();++i)if(micName(i)==microphone){mic=i;break;}
        if(mic==UINT(WAVE_MAPPER)){notify(L"Selected microphone disconnected. Choose another in Settings.");return;}
    }
    target=GetForegroundWindow();nativeEditor=scintillaFocus(target);changedFocus=false;
    if(automation)automation->GetFocusedElement(&field);
    escapeHook=SetWindowsHookExW(WH_KEYBOARD_LL,onEscape,instance,0);
    if(!escapeHook){releaseTarget();notify(L"Could not enable Esc cancellation. Try dictation again.");return;}
    if(!recorder.open(mic)){idle();notify(L"Microphone unavailable. Check Windows microphone permissions and Settings.");return;}
    focusHook=SetWinEventHook(EVENT_OBJECT_FOCUS,EVENT_OBJECT_FOCUS,nullptr,focusEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    windowHook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,focusEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    started=GetTickCount64();setState(State::Listening);SetTimer(window,1,20,nullptr);
}
void processRecording(std::string wav) {
    if(wav.empty()){idle();notify(L"No speech detected.");return;}
    ResetEvent(cancelled.h);setState(State::Transcribing);
    pipeline=std::thread([wav=std::move(wav)]() mutable {
        auto result=std::make_unique<Result>();
        try {
            auto raw=runWorker(exe.parent_path()/L"whisper-cli.exe",L"-m ggml-base.en-q5_1.bin -l en -t 6 -np -nt -sns -of transcript -f -",models,wav,job.h,cancelled.h);
            std::string().swap(wav);
            result->text=wide(raw);
            if(result->text.empty()) result->message=L"No speech detected.";
            else {
                PostMessageW(window,Polishing,0,0);
                auto clean=wide(runWorker(exe.parent_path()/L"polish.exe",Language,models,raw,job.h,cancelled.h));
                if(clean.empty()) throw std::runtime_error("empty");
                result->text=std::move(clean);result->insert=true;
            }
        } catch(...) {
            result->message=result->text.empty()?L"Transcription failed. Check the model setup and try again.":L"Polishing failed. Use Copy last dictation for the original transcript.";
        }
        PostMessageW(window,Finished,0,reinterpret_cast<LPARAM>(result.release()));
    });
}
void finishRecording() { processRecording(recorder.finish()); }
UINT mods(WORD value) {
    auto h=HIBYTE(value);
    return MOD_NOREPEAT|((h&HOTKEYF_ALT)?MOD_ALT:0)|((h&HOTKEYF_CONTROL)?MOD_CONTROL:0)|((h&HOTKEYF_SHIFT)?MOD_SHIFT:0);
}
bool bind(WORD value) {
    if(value==chord && registered)return true;
    int next=binding==1?2:1;
    if(!RegisterHotKey(window,next,mods(value),LOBYTE(value)))return false;
    if(registered)UnregisterHotKey(window,binding);
    registered=true;binding=next;chord=value;return true;
}
bool startup(bool write=false,bool enabled=false) {
    const wchar_t *key=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if(!write){DWORD size=0;return RegGetValueW(HKEY_CURRENT_USER,key,L"QuietType",RRF_RT_REG_SZ,nullptr,nullptr,&size)==ERROR_SUCCESS;}
    HKEY k;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,key,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr)!=ERROR_SUCCESS)return false;
    auto cmd=L"\""+exe.wstring()+L"\"";
    LONG ret=enabled?RegSetValueExW(k,L"QuietType",0,REG_SZ,(BYTE*)cmd.c_str(),(cmd.size()+1)*2):RegDeleteValueW(k,L"QuietType");
    RegCloseKey(k);return ret==ERROR_SUCCESS||(!enabled&&ret==ERROR_FILE_NOT_FOUND);
}
INT_PTR CALLBACK preferences(HWND h,UINT m,WPARAM w,LPARAM l) {
    static visual::Settings theme;
    if(m==WM_PAINT){
        PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);theme.surface(h,dc);EndPaint(h,&ps);return TRUE;
    }
    if(m==WM_CTLCOLORSTATIC || m==WM_CTLCOLORBTN)return theme.color((HWND)l,(HDC)w);
    if(m==WM_NOTIFY && theme.action(h,(NMHDR*)l))return TRUE;
    if(m==WM_DESTROY){theme.close();return FALSE;}
    if(m==WM_INITDIALOG) {
        dialog=h;theme.init(h);
        SetDlgItemTextW(h,112,setup.h?L"Setting up offline models":ready()?L"Offline models ready":L"Offline models need setup");
        SendDlgItemMessageW(h,101,HKM_SETHOTKEY,chord,0);
        CheckRadioButton(h,102,105,toggle?105:102);
        SendDlgItemMessageW(h,103,CB_ADDSTRING,0,(LPARAM)L"Windows default");
        int selected=0;
        for(UINT i=0;i<waveInGetNumDevs();++i){
            auto name=micName(i); if(name.empty()) continue;
            int index=SendDlgItemMessageW(h,103,CB_ADDSTRING,0,(LPARAM)name.c_str());
            if(name==microphone)selected=index;
        }
        SendDlgItemMessageW(h,103,CB_SETCURSEL,selected,0);
        CheckDlgButton(h,104,startup()?BST_CHECKED:BST_UNCHECKED);return TRUE;
    }
    if(m==WM_COMMAND && LOWORD(w)==IDOK){
        WORD value=SendDlgItemMessageW(h,101,HKM_GETHOTKEY,0,0);
        if(!LOBYTE(value)||LOBYTE(value)==VK_ESCAPE||(!HIBYTE(value) && (LOBYTE(value)<VK_F1||LOBYTE(value)>VK_F24))||!bind(value)){
            MessageBoxW(h,L"Choose an available combination with Ctrl, Alt or Shift, or a function key. Your previous shortcut is still active if available.",L"Shortcut unavailable",MB_ICONINFORMATION);return TRUE;
        }
        toggle=IsDlgButtonChecked(h,105)==BST_CHECKED;
        int index=SendDlgItemMessageW(h,103,CB_GETCURSEL,0,0);
        wchar_t name[MAXPNAMELEN+1]{};if(index>0)SendDlgItemMessageW(h,103,CB_GETLBTEXT,index,(LPARAM)name);microphone=name;
        WritePrivateProfileStringW(L"dictation",L"shortcut",std::to_wstring(chord).c_str(),ini.c_str());
        WritePrivateProfileStringW(L"dictation",L"toggle",toggle?L"1":L"0",ini.c_str());
        WritePrivateProfileStringW(L"dictation",L"mic",name,ini.c_str());
        if(!startup(true,IsDlgButtonChecked(h,104)==BST_CHECKED))notify(L"Windows could not save the sign-in setting.");
        EndDialog(h,IDOK);return TRUE;
    }
    if(m==WM_CLOSE || (m==WM_COMMAND && LOWORD(w)==IDCANCEL)){EndDialog(h,IDCANCEL);return TRUE;}
    return FALSE;
}
void settings() {
    if(dialog){SetForegroundWindow(dialog);return;}
    if(state!=State::Idle)return;
    DialogBoxW(instance,MAKEINTRESOURCEW(100),window,preferences);dialog=nullptr;
}
void menu() {
    HMENU m=CreatePopupMenu();
    AppendMenuW(m,MF_STRING|(state!=State::Idle?MF_GRAYED:0),1,L"Settings…");
    AppendMenuW(m,MF_STRING|(last.empty()?MF_GRAYED:0),2,L"Copy last dictation");
    AppendMenuW(m,MF_STRING|(state!=State::Idle||setup.h?MF_GRAYED:0),3,setup.h?L"Setting up models…":L"Set up models…");
    AppendMenuW(m,MF_SEPARATOR,0,nullptr);AppendMenuW(m,MF_STRING,4,L"Quit QuietType");
    SetMenuDefaultItem(m,1,FALSE);
    POINT p;GetCursorPos(&p);SetForegroundWindow(window);
    int id=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,window,nullptr);
    DestroyMenu(m);PostMessageW(window,WM_NULL,0,0);
    if(id==1)settings();if(id==2&&!copyText(last))notify(L"Clipboard is busy. Try copying again.");
    if(id==3)setupModels();if(id==4)PostMessageW(window,WM_CLOSE,0,0);
}
LRESULT CALLBACK dispatch(HWND h,UINT m,WPARAM w,LPARAM l) {
    static UINT taskbar=RegisterWindowMessageW(L"TaskbarCreated");
    if(m==taskbar){addIcon();return 0;}
    switch(m){
    case FirstRun: if(!registered){MessageBoxW(nullptr,L"The dictation shortcut is in use. Choose a different shortcut in Settings.",L"QuietType",MB_ICONINFORMATION);settings();} if(!ready())setupModels();return 0;
    case Tray: if(l==WM_RBUTTONUP)menu();else if(l==WM_LBUTTONDBLCLK)settings();return 0;
    case CancelDictation: stopWork();return 0;
    case WM_HOTKEY:
        if(w==static_cast<WPARAM>(binding)&&!dialog&&!quitting){
            if(state==State::Idle)startRecording();else if(toggle&&state==State::Listening)finishRecording();
        }return 0;
    case WM_TIMER:
        if(w==2 && setup.h && WaitForSingleObject(setup.h,0)==WAIT_OBJECT_0){
            DWORD code;GetExitCodeProcess(setup.h,&code);setup.reset();KillTimer(h,2);
            if(dialog)SetDlgItemTextW(dialog,112,ready()?L"Offline models ready":L"Offline models need setup");
            notify(code==0&&ready()?L"Models ready. Use your shortcut in a text field.":L"Model setup failed. Choose Set up models to retry or resume.");
        }
        if(w!=1)return 0;
        if(state==State::Listening){
            if(!recorder.drain()||GetTickCount64()-recorder.lastBuffer>2500){stopWork();notify(L"Microphone stopped responding.");}
            else if(GetTickCount64()-started>=300000 || (!toggle && (!(GetAsyncKeyState(LOBYTE(chord))&0x8000) ||
                ((mods(chord)&MOD_CONTROL)&&!(GetAsyncKeyState(VK_CONTROL)&0x8000)) ||
                ((mods(chord)&MOD_SHIFT)&&!(GetAsyncKeyState(VK_SHIFT)&0x8000)) ||
                ((mods(chord)&MOD_ALT)&&!(GetAsyncKeyState(VK_MENU)&0x8000)))))finishRecording();
        } else if(state==State::Delivering){
            bool held=false;
            for(int key:{VK_CONTROL,VK_MENU,VK_SHIFT,VK_LWIN,VK_RWIN})held|=(GetAsyncKeyState(key)&0x8000)!=0;
            if(held && GetTickCount64()-started<1500)return 0;
            if(held || !paste())notify(L"Text ready. Use Copy last dictation from the tray.");
            idle();
        }return 0;
    case Polishing: if(!quitting && WaitForSingleObject(cancelled.h,0)!=WAIT_OBJECT_0)setState(State::Polishing);return 0;
    case Finished:{
        std::unique_ptr<Result> result(reinterpret_cast<Result*>(l));pipeline.join();
        if(WaitForSingleObject(cancelled.h,0)==WAIT_OBJECT_0)idle();
        else{
            if(!result->text.empty())last=std::move(result->text);
            if(result->insert){started=GetTickCount64();setState(State::Delivering);}
            else{idle();notify(result->message);}
        }
        if(quitting)DestroyWindow(h);return 0;
    }
    case WM_CLOSE: quitting=true;stopWork();if(!pipeline.joinable())DestroyWindow(h);return 0;
    case WM_DESTROY: Shell_NotifyIconW(NIM_DELETE,&icon);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,PWSTR command,int) {
    if(wcscmp(command,L"--remove-startup")==0)return startup(true,false)?0:1;
    Handle single(CreateMutexW(nullptr,FALSE,L"Local\\QuietType.Tray.1"));
    if(!single.h || GetLastError()==ERROR_ALREADY_EXISTS)return 0;
    instance=inst;CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    CoCreateInstance(__uuidof(CUIAutomation),nullptr,CLSCTX_INPROC_SERVER,__uuidof(IUIAutomation),(void**)&automation);
    wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);exe=path;
    PWSTR local=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)))return 1;
    folder=fs::path(local)/L"QuietType";CoTaskMemFree(local);models=folder/L"models";ini=folder/L"settings.ini";
    std::error_code ec;fs::create_directories(folder,ec);if(ec)return 1;
    chord=GetPrivateProfileIntW(L"dictation",L"shortcut",chord,ini.c_str());
    toggle=GetPrivateProfileIntW(L"dictation",L"toggle",0,ini.c_str())!=0;
    wchar_t mic[MAXPNAMELEN+1]{};GetPrivateProfileStringW(L"dictation",L"mic",L"",mic,MAXPNAMELEN+1,ini.c_str());microphone=mic;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_HOTKEY_CLASS|ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSW wc{};wc.hInstance=inst;wc.lpfnWndProc=dispatch;wc.lpszClassName=L"QuietType.Tray";RegisterClassW(&wc);
    window=CreateWindowW(wc.lpszClassName,L"QuietType",WS_POPUP,0,0,0,0,nullptr,nullptr,inst,nullptr);
    wc.lpfnWndProc=paint;wc.lpszClassName=L"QuietType.Status";RegisterClassW(&wc);
    indicator=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT,wc.lpszClassName,L"",WS_POPUP,0,0,172,40,nullptr,nullptr,inst,nullptr);
    job.h=CreateJobObjectW(nullptr,nullptr);cancelled.h=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!window||!indicator||!cancelled.h||!SetInformationJobObject(job.h,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))return 1;
    addIcon();bind(chord);PostMessageW(window,FirstRun,0,0);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    releaseTarget();if(automation)automation->Release();CoUninitialize();return 0;
}
