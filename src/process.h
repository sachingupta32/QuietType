#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <stdexcept>

struct Handle {
    HANDLE h{};
    Handle()=default;
    explicit Handle(HANDLE v):h(v){}
    ~Handle(){reset();}
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    void reset(){ if(h && h!=INVALID_HANDLE_VALUE) CloseHandle(h); h=nullptr; }
};

inline std::string runWorker(const std::filesystem::path &exe,const std::wstring &args,
                const std::filesystem::path &directory,const std::string &input,HANDLE job,HANDLE cancelled) {
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    Handle inRead,inWrite,outRead,outWrite;
    if (!CreatePipe(&inRead.h,&inWrite.h,&sa,0) || !CreatePipe(&outRead.h,&outWrite.h,&sa,0)
        || !SetHandleInformation(inWrite.h,HANDLE_FLAG_INHERIT,0) || !SetHandleInformation(outRead.h,HANDLE_FLAG_INHERIT,0))
        throw std::runtime_error("pipe");
    Handle null(CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr));
    STARTUPINFOW si{sizeof(si)}; si.dwFlags=STARTF_USESTDHANDLES;
    si.hStdInput=inRead.h; si.hStdOutput=outWrite.h; si.hStdError=null.h;
    PROCESS_INFORMATION pi{};
    auto command=L"\""+exe.wstring()+L"\" "+args;
    if (!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,
                        nullptr,directory.c_str(),&si,&pi)) throw std::runtime_error("launch");
    Handle child(pi.hProcess),thread(pi.hThread);
    if (!AssignProcessToJobObject(job,child.h) || WaitForSingleObject(cancelled,0)==WAIT_OBJECT_0
        || ResumeThread(thread.h)==DWORD(-1)) {
        TerminateProcess(child.h,1); WaitForSingleObject(child.h,INFINITE); throw std::runtime_error("cancel");
    }
    inRead.reset(); outWrite.reset();
    DWORD wrote=0;
    BOOL sent=WriteFile(inWrite.h,input.data(),static_cast<DWORD>(input.size()),&wrote,nullptr);
    inWrite.reset();
    std::string result;
    char block[4096]; DWORD n;
    while(ReadFile(outRead.h,block,sizeof(block),&n,nullptr) && n) {
        if(result.size()+n>128*1024) {TerminateProcess(child.h,1); break;}
        result.append(block,n);
    }
    WaitForSingleObject(child.h,INFINITE);
    DWORD status=1; GetExitCodeProcess(child.h,&status);
    if(!sent || wrote!=input.size() || status) throw std::runtime_error("worker");
    auto start=result.find_first_not_of(" \n\r\t");
    if(start==std::string::npos) return {};
    return result.substr(start,result.find_last_not_of(" \n\r\t")-start+1);
}
