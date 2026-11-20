#pragma once
#include <windows.h>
#include <commctrl.h>
#include <cmath>

namespace visual {
constexpr COLORREF pine=RGB(23,59,56), mint=RGB(172,236,201), paper=RGB(250,249,245),
    ink=RGB(31,49,46), muted=RGB(81,99,94), line=RGB(212,219,211);

inline bool highContrast() {
    HIGHCONTRASTW hc{sizeof(hc)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(hc),&hc,0) && (hc.dwFlags&HCF_HIGHCONTRASTON);
}
inline int dpi(HWND h) {
    HDC dc=GetDC(h);int value=GetDeviceCaps(dc,LOGPIXELSY);ReleaseDC(h,dc);return value;
}
inline HFONT font(HWND h,int points,int weight) {
    return CreateFontW(-MulDiv(points,dpi(h),72),0,0,0,weight,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
}
inline void fill(HDC dc,RECT r,COLORREF color) {
    SetDCBrushColor(dc,color);FillRect(dc,&r,(HBRUSH)GetStockObject(DC_BRUSH));
}
inline RECT units(HWND h,int x,int y,int right,int bottom) {
    RECT r{x,y,right,bottom};MapDialogRect(h,&r);return r;
}
struct Settings {
    HFONT title{},label{},small{};
    HBRUSH body{},header{};
    bool hc{};
    void init(HWND h) {
        hc=highContrast();
        body=CreateSolidBrush(hc?GetSysColor(COLOR_WINDOW):paper);
        header=CreateSolidBrush(hc?GetSysColor(COLOR_WINDOW):pine);
        title=font(h,23,FW_SEMIBOLD);label=font(h,10,FW_SEMIBOLD);small=font(h,9,FW_NORMAL);
        for(int id:{110,113,114,115,IDOK})SendDlgItemMessageW(h,id,WM_SETFONT,(WPARAM)label,TRUE);
        SendDlgItemMessageW(h,111,WM_SETFONT,(WPARAM)title,TRUE);
        for(int id:{112,120,121,122})SendDlgItemMessageW(h,id,WM_SETFONT,(WPARAM)small,TRUE);
        SendMessageW(h,WM_SETICON,ICON_SMALL,(LPARAM)LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(2)));
    }
    void close() {
        for(auto f:{title,label,small})DeleteObject(f);
        DeleteObject(body);DeleteObject(header);
    }
    void surface(HWND h,HDC dc) const {
        RECT all{};GetClientRect(h,&all);FillRect(dc,&all,body);
        RECT hero=units(h,0,0,374,85);FillRect(dc,&hero,header);
        if(!hc) {
            // A repeating voiceprint, clipped to the header's right edge.
            for(int i=0;i<49;i++){
                double x=i/48.0;
                double envelope=0.50+0.50*std::sin(8.2*x-1.0);
                int amplitude=3+int(24*envelope*(0.38+0.62*std::abs(std::sin(6.5*x+0.1))));
                fill(dc,units(h,251+3*i,42-amplitude,252+3*i,42+amplitude),RGB(73,123,103));
            }
        }
        auto divider=hc?GetSysColor(COLOR_WINDOWTEXT):line;
        for(int y:{133,176,219}) {
            RECT r=units(h,23,y,350,y);r.bottom++;fill(dc,r,divider);
        }
        RECT footer=units(h,0,264,374,265);footer.bottom=footer.top+1;fill(dc,footer,divider);
    }
    INT_PTR color(HWND control,HDC dc) const {
        int id=GetDlgCtrlID(control);
        bool inHeader=id>=110 && id<=112;
        COLORREF fg=hc?GetSysColor(COLOR_WINDOWTEXT):(inHeader?(id==111?RGB(251,250,244):mint):(id>=120?muted:ink));
        SetTextColor(dc,fg);SetBkMode(dc,TRANSPARENT);
        return (INT_PTR)(inHeader?header:body);
    }
    bool action(HWND h,NMHDR *hdr) const {
        if(hc || hdr->code!=NM_CUSTOMDRAW || (hdr->idFrom!=IDOK && hdr->idFrom!=IDCANCEL &&
            hdr->idFrom!=102 && hdr->idFrom!=105))return false;
        auto *draw=reinterpret_cast<NMCUSTOMDRAW*>(hdr);
        if(draw->dwDrawStage!=CDDS_PREPAINT)return false;
        bool selected=hdr->idFrom==IDOK || SendMessageW(hdr->hwndFrom,BM_GETCHECK,0,0)==BST_CHECKED;
        bool hover=(draw->uItemState&CDIS_HOT)!=0,down=(draw->uItemState&CDIS_SELECTED)!=0;
        HDC dc=draw->hdc;RECT r=draw->rc;int saved=SaveDC(dc);
        fill(dc,r,paper);SelectObject(dc,GetStockObject(DC_PEN));SelectObject(dc,GetStockObject(DC_BRUSH));
        SetDCPenColor(dc,selected?pine:line);
        SetDCBrushColor(dc,selected?(down?RGB(15,44,40):hover?RGB(39,80,72):pine):(hover||down?RGB(235,240,233):RGB(255,255,252)));
        RoundRect(dc,r.left,r.top,r.right,r.bottom,12,12);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,selected?RGB(255,255,250):ink);
        SelectObject(dc,(HFONT)SendMessageW(hdr->hwndFrom,WM_GETFONT,0,0));
        wchar_t text[64]{};GetWindowTextW(hdr->hwndFrom,text,64);
        UINT cues=SendMessageW(hdr->hwndFrom,WM_QUERYUISTATE,0,0);
        DrawTextW(dc,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|((cues&UISF_HIDEACCEL)?DT_HIDEPREFIX:0));
        if((draw->uItemState&CDIS_FOCUS) && !(cues&UISF_HIDEFOCUS)){InflateRect(&r,-4,-4);DrawFocusRect(dc,&r);}
        RestoreDC(dc,saved);SetWindowLongPtrW(h,DWLP_MSGRESULT,CDRF_SKIPDEFAULT);return true;
    }
};
}
