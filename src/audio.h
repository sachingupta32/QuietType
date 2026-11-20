#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <array>
#include <string>
#include <cmath>

class Recorder {
    HWAVEIN device{};
    std::array<WAVEHDR,8> headers{};
    std::array<std::array<char,3200>,8> buffers{};
    unsigned next{};
    std::string pcm;
public:
    ULONGLONG lastBuffer{};
    bool open(UINT mic) {
        pcm.clear(); next=0; headers={};
        WAVEFORMATEX format{WAVE_FORMAT_PCM,1,16000,32000,2,16,0};
        if (waveInOpen(&device,mic,&format,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR) return false;
        for (size_t i=0;i<headers.size();++i) {
            auto &h=headers[i]; h.lpData=buffers[i].data(); h.dwBufferLength=buffers[i].size();
            if (waveInPrepareHeader(device,&h,sizeof(h)) || waveInAddBuffer(device,&h,sizeof(h))) { close(); return false; }
        }
        lastBuffer=GetTickCount64();
        if (waveInStart(device)) { close(); return false; }
        return true;
    }
    bool drain(bool requeue=true) {
        for (unsigned i=0;i<headers.size();++i) {
            auto &h=headers[next];
            if (!(h.dwFlags & WHDR_DONE)) break;
            pcm.append(h.lpData,h.dwBytesRecorded);
            lastBuffer=GetTickCount64();
            next=(next+1)%headers.size();
            if (requeue && waveInAddBuffer(device,&h,sizeof(h))) return false;
        }
        return true;
    }
    void close() {
        if (!device) return;
        waveInReset(device); drain(false);
        for (auto &h:headers) if(h.dwFlags & WHDR_PREPARED) waveInUnprepareHeader(device,&h,sizeof(h));
        waveInClose(device); device=nullptr;
    }
    void discard() { close(); std::string().swap(pcm); }
    std::string finish() {
        close();
        std::string samples; samples.swap(pcm);
        // Count energetic 20 ms frames rather than accepting a single click.
        unsigned voiced=0;
        for(size_t i=0;i+640<=samples.size();i+=640) {
            double sum=0;
            for(size_t j=i;j<i+640;j+=2) {
                short sample; memcpy(&sample,samples.data()+j,2); sum+=double(sample)*sample;
            }
            if(sum>320.0*90*90) ++voiced;
        }
        if(voiced<5) return {};
        std::string wav="RIFF";
        auto number=[&](DWORD value,int bytes){wav.append(reinterpret_cast<const char*>(&value),bytes);};
        number(36+samples.size(),4); wav+="WAVEfmt "; number(16,4); number(1,2); number(1,2);
        number(16000,4); number(32000,4); number(2,2); number(16,2); wav+="data"; number(samples.size(),4);
        wav+=samples; return wav;
    }
    ~Recorder(){discard();}
};
