#pragma once
#include <windows.h>
#include <commctrl.h>
#include <cmath>

namespace visual {
constexpr COLORREF ink=RGB(23,22,27), lime=RGB(221,248,72), paper=RGB(241,241,239),
    lavender=RGB(222,214,249), card=RGB(255,255,255), muted=RGB(92,88,105), line=RGB(226,224,231);

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
    HBRUSH body{},header{},fieldCard{},badge{};
    bool hc{};
    void init(HWND h) {
        hc=highContrast();
        body=CreateSolidBrush(hc?GetSysColor(COLOR_WINDOW):paper);
        header=CreateSolidBrush(hc?GetSysColor(COLOR_WINDOW):lavender);
        fieldCard=CreateSolidBrush(hc?GetSysColor(COLOR_WINDOW):card);
        badge=CreateSolidBrush(hc?GetSysColor(COLOR_WINDOW):card);
        title=font(h,26,FW_LIGHT);label=font(h,10,FW_SEMIBOLD);small=font(h,9,FW_NORMAL);
        for(int id:{110,113,114,115,IDOK})SendDlgItemMessageW(h,id,WM_SETFONT,(WPARAM)label,TRUE);
        SendDlgItemMessageW(h,111,WM_SETFONT,(WPARAM)title,TRUE);
        for(int id:{112,120,121,122,123})SendDlgItemMessageW(h,id,WM_SETFONT,(WPARAM)small,TRUE);
        SendMessageW(h,WM_SETICON,ICON_SMALL,(LPARAM)LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(2)));
    }
    void close() {
        for(auto f:{title,label,small})DeleteObject(f);
        DeleteObject(body);DeleteObject(header);DeleteObject(fieldCard);DeleteObject(badge);
    }
    void surface(HWND h,HDC dc) const {
        RECT all{};GetClientRect(h,&all);FillRect(dc,&all,body);
        RECT hero=units(h,12,6,362,90);
        if(!hc) {
            int saved=SaveDC(dc);
            HRGN shape=CreateRoundRectRgn(hero.left,hero.top,hero.right,hero.bottom,34,34);
            SelectClipRgn(dc,shape);DeleteObject(shape);
            FillRect(dc,&hero,header);
            RECT fade=units(h,215,6,362,90);
            for(int x=fade.left;x<fade.right;++x) {
                int t=(x-fade.left)*100/(fade.right-fade.left);
                fill(dc,{x,fade.top,x+1,fade.bottom},RGB(222-t*42/100,214-t*48/100,249-t*15/100));
            }
            RestoreDC(dc,saved);
        }
        if(!hc) {
            // A repeating voiceprint, clipped to the header's right edge.
            for(int i=0;i<37;i++){
                double x=i/36.0;
                double envelope=0.50+0.50*std::sin(8.2*x-1.0);
                int amplitude=3+int(24*envelope*(0.38+0.62*std::abs(std::sin(6.5*x+0.1))));
                fill(dc,units(h,244+3*i,42-amplitude,245+3*i,42+amplitude),RGB(102,88,147));
            }
        }
        if(!hc) {
            int saved=SaveDC(dc);
            SelectObject(dc,GetStockObject(DC_PEN));
            SelectObject(dc,GetStockObject(DC_BRUSH));
            SetDCPenColor(dc,line);
            SetDCBrushColor(dc,card);
            auto panel=units(h,23,99,351,225);
            RoundRect(dc,panel.left,panel.top,panel.right,panel.bottom,30,30);
            SetDCPenColor(dc,card);
            SetDCBrushColor(dc,card);
            auto pill=units(h,23,60,142,79);
            RoundRect(dc,pill.left,pill.top,pill.right,pill.bottom,28,28);
            RestoreDC(dc,saved);
        }
        auto divider=hc?GetSysColor(COLOR_WINDOWTEXT):line;
        for(int y:{140,180}) {
            RECT r=units(h,34,y,340,y);r.bottom++;fill(dc,r,divider);
        }
        RECT footer=units(h,23,268,351,269);footer.bottom=footer.top+1;fill(dc,footer,divider);
    }
    INT_PTR color(HWND control,HDC dc) const {
        int id=GetDlgCtrlID(control);
        bool inHeader=id>=110 && id<=112;
        COLORREF fg=hc?GetSysColor(COLOR_WINDOWTEXT):(id>=120?muted:ink);
        SetTextColor(dc,fg);SetBkMode(dc,TRANSPARENT);
        if(id==112)return (INT_PTR)badge;
        bool inCard=(id>=113 && id<=115) || (id>=120 && id<=122);
        return (INT_PTR)(inHeader?header:inCard?fieldCard:body);
    }
    bool action(HWND h,NMHDR *hdr) const {
        if(hc || hdr->code!=NM_CUSTOMDRAW || (hdr->idFrom!=IDOK && hdr->idFrom!=IDCANCEL &&
            hdr->idFrom!=102 && hdr->idFrom!=105))return false;
        auto *draw=reinterpret_cast<NMCUSTOMDRAW*>(hdr);
        if(draw->dwDrawStage!=CDDS_PREPAINT)return false;
        bool primary=hdr->idFrom==IDOK;
        bool active=SendMessageW(hdr->hwndFrom,BM_GETCHECK,0,0)==BST_CHECKED;
        bool hover=(draw->uItemState&CDIS_HOT)!=0,down=(draw->uItemState&CDIS_SELECTED)!=0;
        HDC dc=draw->hdc;RECT r=draw->rc;int saved=SaveDC(dc);
        fill(dc,r,(hdr->idFrom==102 || hdr->idFrom==105)?card:paper);
        SelectObject(dc,GetStockObject(DC_PEN));SelectObject(dc,GetStockObject(DC_BRUSH));
        COLORREF bg=primary?(hover||down?RGB(52,48,64):ink):
            active?(hover||down?RGB(207,196,245):lavender):(hover||down?paper:card);
        SetDCPenColor(dc,primary||active?bg:line);SetDCBrushColor(dc,bg);
        int height=r.bottom-r.top;
        RoundRect(dc,r.left,r.top,r.right,r.bottom,height,height);
        RECT labelRect=r;
        if(primary) {
            int inset=height/8;
            int right=r.right-inset, left=r.right-height+inset;
            SetDCPenColor(dc,lime);SetDCBrushColor(dc,lime);
            Ellipse(dc,left,r.top+inset,right,r.bottom-inset);
            SetDCPenColor(dc,ink);
            int x=(left+right)/2,y=(r.top+r.bottom)/2,arrow=height/8;
            MoveToEx(dc,x-arrow,y,nullptr);LineTo(dc,x+arrow,y);
            MoveToEx(dc,x,y-arrow,nullptr);LineTo(dc,x+arrow,y);LineTo(dc,x,y+arrow);
            labelRect.right=left;labelRect.left+=inset;
        }
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,primary?card:ink);
        SelectObject(dc,(HFONT)SendMessageW(hdr->hwndFrom,WM_GETFONT,0,0));
        wchar_t text[64]{};GetWindowTextW(hdr->hwndFrom,text,64);
        UINT cues=SendMessageW(hdr->hwndFrom,WM_QUERYUISTATE,0,0);
        DrawTextW(dc,text,-1,&labelRect,DT_CENTER|DT_VCENTER|DT_SINGLELINE|((cues&UISF_HIDEACCEL)?DT_HIDEPREFIX:0));
        if((draw->uItemState&CDIS_FOCUS) && !(cues&UISF_HIDEFOCUS)){InflateRect(&r,-4,-4);DrawFocusRect(dc,&r);}
        RestoreDC(dc,saved);SetWindowLongPtrW(h,DWLP_MSGRESULT,CDRF_SKIPDEFAULT);return true;
    }
};
}
