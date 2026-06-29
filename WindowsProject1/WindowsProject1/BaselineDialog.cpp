#include "framework.h"
#include "BaselineDialog.h"
#include "BaselineEngine.h"
#include "BaselineCommon.h"
#include <commdlg.h>
#include <fstream>

#pragma comment(lib, "Comdlg32.lib")

// ---------------------------------------------------------------------------
// Resource IDs
// ---------------------------------------------------------------------------
#define IDC_BASELINE_LIST      2001
#define IDC_BTN_RUN_BASELINE   2002
#define IDC_BTN_EXPORT_REPORT  2003
#define IDC_BTN_CLOSE          2004

#define BASELINE_HEADER_H      52

// Color constants (match main window style)
#define CLR_BG          RGB( 15,  23,  42)
#define CLR_HEADER      RGB( 22,  33,  62)
#define CLR_ACCENT      RGB( 59, 130, 246)
#define CLR_TXT_MAIN    RGB(248, 250, 252)
#define CLR_TXT_SUB     RGB(100, 116, 139)
#define CLR_LIST_BG     RGB( 22,  33,  62)
#define CLR_LIST_TXT    RGB(226, 232, 240)
#define CLR_BTN_QS      RGB( 34, 197,  94)
#define CLR_BTN_QS_P    RGB( 21, 128,  61)
#define CLR_BTN_CS      RGB( 59, 130, 246)
#define CLR_BTN_CS_P    RGB( 29,  78, 216)
#define CLR_BTN_BL      RGB(139,  92, 246)
#define CLR_BTN_BL_P    RGB(109,  40, 217)
#define CLR_BTN_RED     RGB(239,  68,  68)
#define CLR_BTN_RED_P   RGB(185,  28,  28)
#define CLR_BTN_DIS     RGB( 51,  65,  85)
#define CLR_COMPLIANT   RGB( 34, 197,  94)
#define CLR_LOW_RISK    RGB(234, 179,   8)
#define CLR_MED_RISK    RGB(245, 158,  11)
#define CLR_HIGH_RISK   RGB(239,  68,  68)
#define CLR_CRIT_RISK   RGB(220,  38,  38)

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static std::vector<BaselineResult> g_results;
static HWND  g_hList       = nullptr;
static HWND  g_hBtnRun     = nullptr;
static HWND  g_hBtnExport  = nullptr;
static HWND  g_hBtnClose   = nullptr;
static HBRUSH g_hBrushList = nullptr;
static HFONT  g_hFontTitle = nullptr;
static HFONT  g_hFontBtn   = nullptr;
static HFONT  g_hFontMono  = nullptr;
static bool   g_bScanDone  = false;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static HFONT MakeFont(int ptSize, bool bold, const wchar_t* face)
{
    return CreateFontW(
        -MulDiv(ptSize, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72),
        0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

static const wchar_t* RiskColor(RiskLevel level)
{
    switch (level) {
    case RiskLevel::Compliant: return L"合规";
    case RiskLevel::Low:       return L"低风险";
    case RiskLevel::Medium:    return L"中风险";
    case RiskLevel::High:      return L"高风险";
    case RiskLevel::Critical:  return L"严重";
    default:                   return L"未知";
    }
}

static COLORREF RiskColorRef(RiskLevel level)
{
    switch (level) {
    case RiskLevel::Compliant: return CLR_COMPLIANT;
    case RiskLevel::Low:       return CLR_LOW_RISK;
    case RiskLevel::Medium:    return CLR_MED_RISK;
    case RiskLevel::High:      return CLR_HIGH_RISK;
    case RiskLevel::Critical:  return CLR_CRIT_RISK;
    default:                   return CLR_TXT_SUB;
    }
}

// ---------------------------------------------------------------------------
// Show
// ---------------------------------------------------------------------------

void BaselineDialog::Show(HWND hParent)
{
    static const wchar_t* kClassName = L"BaselineDlgClass";

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = BaselineDialog::WndProc;
    wc.hInstance     = (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(0, kClassName, L"系统基线安全检测",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 740, 560,
        hParent, nullptr, (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE), nullptr);
    if (hWnd) {
        ShowWindow(hWnd, SW_SHOW);
        SetForegroundWindow(hWnd);

        // Message loop
        MSG msg;
        while (IsWindow(hWnd) && GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // Cleanup class
        UnregisterClassW(kClassName, (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE));
    }
}

// ---------------------------------------------------------------------------
// RunChecks (background thread)
// ---------------------------------------------------------------------------

struct ScanParam {
    HWND hWnd;
};
static DWORD WINAPI ScanThread(LPVOID param)
{
    HWND hWnd = ((ScanParam*)param)->hWnd;
    delete (ScanParam*)param;

    g_results = BaselineEngine::RunAllChecks();

    PostMessageW(hWnd, WM_APP + 1, 0, 0);
    return 0;
}

void BaselineDialog::RunChecks(HWND hWnd)
{
    g_results.clear();
    g_bScanDone = false;
    EnableWindow(g_hBtnRun, FALSE);
    SetWindowTextW(hWnd, L"系统基线安全检测 - 检测中...");

    // Clear list
    SendMessageW(g_hList, LB_RESETCONTENT, 0, 0);
    SendMessageW(g_hList, LB_ADDSTRING, 0, (LPARAM)L"正在检测，请稍候...");

    ScanParam* param = new ScanParam{hWnd};
    HANDLE hThread = CreateThread(nullptr, 0, ScanThread, param, 0, nullptr);
    if (hThread) CloseHandle(hThread);
}

// ---------------------------------------------------------------------------
// UpdateList
// ---------------------------------------------------------------------------

void BaselineDialog::UpdateList(HWND hWnd)
{
    SendMessageW(g_hList, LB_RESETCONTENT, 0, 0);
    g_bScanDone = true;
    EnableWindow(g_hBtnRun, TRUE);
    SetWindowTextW(hWnd, L"系统基线安全检测");

    int compliantCount = 0;
    int riskCount      = 0;

    for (auto& r : g_results) {
        // Format: [风险等级] 检测项名称 | 实际值 | 标准
        std::wstring line;
        line += L"[";
        line += RiskColor(r.riskLevel);
        line += L"] ";
        line += r.checkName;
        line += L" | ";
        line += r.actualValue.empty() ? L"未检测" : r.actualValue;

        int idx = (int)SendMessageW(g_hList, LB_ADDSTRING, 0, (LPARAM)line.c_str());
        SendMessageW(g_hList, LB_SETITEMDATA, idx, (LPARAM)(int)r.riskLevel);

        if (r.compliant) compliantCount++;
        else riskCount++;
    }

    // Update window title with summary
    wchar_t title[128];
    swprintf_s(title, L"系统基线安全检测 - %d项合规, %d项风险",
        compliantCount, riskCount);
    SetWindowTextW(hWnd, title);
}

// ---------------------------------------------------------------------------
// ExportReport
// ---------------------------------------------------------------------------

void BaselineDialog::ExportReport(HWND hWnd)
{
    if (g_results.empty()) {
        MessageBoxW(hWnd, L"请先执行基线检测，再导出报告。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize   = sizeof ofn;
    ofn.hwndOwner     = hWnd;
    ofn.lpstrFile     = path;
    ofn.nMaxFile      = MAX_PATH;
    ofn.lpstrFilter   = L"HTML 报告\0*.html\0JSON 报告\0*.json\0全部文件\0*.*\0";
    ofn.lpstrDefExt   = L"html";
    ofn.Flags         = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (!GetSaveFileNameW(&ofn))
        return;

    std::wstring ext = path;
    auto dot = ext.find_last_of(L".");
    std::wstring suffix = (dot == std::wstring::npos) ? L"" : ext.substr(dot);

    bool isJson = (suffix == L".json");

    if (isJson) {
        // Export as JSON
        std::string json = "[\n";
        for (size_t i = 0; i < g_results.size(); ++i) {
            json += ToJson(g_results[i], 1);
            if (i + 1 < g_results.size()) json += ",";
            json += "\n";
        }
        json += "]\n";

        std::ofstream fout(path, std::ios::binary);
        if (fout.is_open()) {
            fout.write(json.data(), json.size());
            fout.close();
        } else {
            MessageBoxW(hWnd, L"无法写入文件。", L"错误", MB_OK | MB_ICONERROR);
            return;
        }
    } else {
        // Export as HTML report
        std::string html;
        html += "<!DOCTYPE html>\n<html lang=\"zh-CN\">\n<head>\n";
        html += "<meta charset=\"UTF-8\">\n";
        html += "<title>系统基线安全检测报告</title>\n";
        html += "<style>\n";
        html += "body{font-family:'Microsoft YaHei',sans-serif;background:#0f172a;color:#f8fafc;margin:0;padding:20px;}\n";
        html += "h1{color:#f8fafc;border-bottom:3px solid #3b82f6;padding-bottom:10px;}\n";
        html += "h2{color:#94a3b8;font-weight:normal;}\n";
        html += "table{width:100%;border-collapse:collapse;margin-top:16px;}\n";
        html += "th,td{padding:10px 14px;text-align:left;border-bottom:1px solid #1e293b;}\n";
        html += "th{background:#16213e;color:#3b82f6;}\n";
        html += "tr:hover{background:#1e293b;}\n";
        html += ".compliant{color:#22c55e;}.low{color:#eab308;}.medium{color:#f59e0b;}.high{color:#ef4444;}.critical{color:#dc2626;}\n";
        html += "#summary{margin:12px 0;padding:8px 16px;background:#16213e;border-radius:8px;display:inline-block;}\n";
        html += "</style>\n</head>\n<body>\n";
        html += "<h1>系统基线安全检测报告</h1>\n";

        // Summary
        int compliant = 0, risks = 0;
        for (auto& r : g_results) {
            if (r.compliant) compliant++;
            else risks++;
        }
        char sum[128];
        sprintf_s(sum, "<div id=\"summary\">总检测项: %zu &nbsp;|&nbsp; 合规: %d &nbsp;|&nbsp; 风险项: %d</div>\n",
                  g_results.size(), compliant, risks);
        html += sum;

        // Timestamp
        time_t now = time(nullptr);
        struct tm timeinfo;
        localtime_s(&timeinfo, &now);
        char ts[64];
        sprintf_s(ts, "<h2>生成时间: %04d-%02d-%02d %02d:%02d:%02d</h2>\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        html += ts;

        html += "<table>\n<tr><th>#</th><th>检测项</th><th>标准</th><th>实际值</th><th>风险等级</th><th>修复建议</th></tr>\n";
        for (size_t i = 0; i < g_results.size(); ++i) {
            auto& r = g_results[i];
            std::string cn  = "<tr>";
            cn += "<td>" + std::to_string(i + 1) + "</td>";

            auto esc = [](const std::wstring& ws) -> std::string {
                // 先用 WideCharToMultiByte 做正确的 UTF-8 编码转换（支持中文等非ASCII字符）
                std::string s;
                if (!ws.empty()) {
                    int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1,
                                                nullptr, 0, nullptr, nullptr);
                    if (n > 0) {
                        s.resize(n - 1);
                        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1,
                                            s.data(), n, nullptr, nullptr);
                    }
                }
                // Simple HTML escape — 逐字符处理，避免 & 替换自身导致的无限循环
                std::string out;
                out.reserve(s.size() + 32);
                for (unsigned char c : s) {
                    switch (c) {
                    case '&': out.push_back('&'); out.append("amp;");  break;
                    case '<': out.push_back('&'); out.append("lt;");   break;
                    case '>': out.push_back('&'); out.append("gt;");   break;
                    default:  out += c;           break;
                    }
                }
                return out;
            };

            const char* rclass = "";
            switch (r.riskLevel) {
            case RiskLevel::Compliant: rclass = "compliant"; break;
            case RiskLevel::Low:       rclass = "low";       break;
            case RiskLevel::Medium:    rclass = "medium";    break;
            case RiskLevel::High:      rclass = "high";      break;
            case RiskLevel::Critical:  rclass = "critical";  break;
            }

            cn += "<td>" + esc(r.checkName) + "</td>";
            cn += "<td>" + esc(r.standard) + "</td>";
            cn += "<td>" + esc(r.actualValue) + "</td>";
            cn += "<td class=\"" + std::string(rclass) + "\">" + esc(RiskColor(r.riskLevel)) + "</td>";
            cn += "<td>" + esc(r.remediation) + "</td>";
            cn += "</tr>\n";
            html += cn;
        }
        html += "</table>\n</body>\n</html>\n";

        std::ofstream fout(path, std::ios::binary);
        if (fout.is_open()) {
            fout.write(html.data(), html.size());
            fout.close();
        } else {
            MessageBoxW(hWnd, L"无法写入文件。", L"错误", MB_OK | MB_ICONERROR);
            return;
        }
    }

    MessageBoxW(hWnd, L"报告导出成功！", L"完成", MB_OK | MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------

LRESULT CALLBACK BaselineDialog::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        g_hBrushList = CreateSolidBrush(CLR_LIST_BG);
        g_hFontTitle = MakeFont(14, true,  L"Microsoft YaHei");
        g_hFontBtn   = MakeFont(11, false, L"Microsoft YaHei");
        g_hFontMono  = MakeFont(11, false, L"Consolas");

        RECT rc; GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        // Listbox
        g_hList = CreateWindowW(L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
            LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_USETABSTOPS,
            10, BASELINE_HEADER_H + 8, w - 20, h - BASELINE_HEADER_H - 8 - 56,
            hWnd, (HMENU)IDC_BASELINE_LIST, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        if (g_hFontBtn) SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);

        // Buttons row at bottom
        int bw = 130;
        int gap = 12;
        int totalW = bw * 3 + gap * 2;
        int startX = (w - totalW) / 2;

        g_hBtnRun = CreateWindowW(L"BUTTON", L"开始检测",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX, h - 46, bw, 36,
            hWnd, (HMENU)IDC_BTN_RUN_BASELINE, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        if (g_hFontBtn) SendMessageW(g_hBtnRun, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);

        g_hBtnExport = CreateWindowW(L"BUTTON", L"导出报告",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX + bw + gap, h - 46, bw, 36,
            hWnd, (HMENU)IDC_BTN_EXPORT_REPORT, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        if (g_hFontBtn) SendMessageW(g_hBtnExport, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);

        g_hBtnClose = CreateWindowW(L"BUTTON", L"关闭",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX + (bw + gap) * 2, h - 46, bw, 36,
            hWnd, (HMENU)IDC_BTN_CLOSE, (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE), nullptr);
        if (g_hFontBtn) SendMessageW(g_hBtnClose, WM_SETFONT, (WPARAM)g_hFontBtn, FALSE);

        // Prompt user to run
        SendMessageW(g_hList, LB_ADDSTRING, 0, (LPARAM)L"点击「开始检测」进行系统基线安全检查");
        break;
    }

    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rc; GetClientRect(hWnd, &rc);
        HBRUSH hbBg = CreateSolidBrush(CLR_BG);
        FillRect(hdc, &rc, hbBg); DeleteObject(hbBg);

        // Header strip
        RECT rcH = { rc.left, rc.top, rc.right, rc.top + BASELINE_HEADER_H };
        HBRUSH hbH = CreateSolidBrush(CLR_HEADER);
        FillRect(hdc, &rcH, hbH); DeleteObject(hbH);

        // Accent line
        RECT rcL = { rc.left, rc.top + BASELINE_HEADER_H, rc.right, rc.top + BASELINE_HEADER_H + 3 };
        HBRUSH hbL = CreateSolidBrush(CLR_ACCENT);
        FillRect(hdc, &rcL, hbL); DeleteObject(hbL);
        return 1;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, CLR_TXT_MAIN);
        if (g_hFontTitle) SelectObject(hdc, g_hFontTitle);
        RECT rcT = { 0, 0, 740, BASELINE_HEADER_H };
        DrawTextW(hdc, L"系统基线安全检测", -1, &rcT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hWnd, &ps);
        break;
    }

    case WM_CTLCOLORLISTBOX:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, CLR_LIST_TXT);
        SetBkColor(hdc, CLR_LIST_BG);
        return (LRESULT)g_hBrushList;
    }

    case WM_MEASUREITEM:
    {
        auto* mis = reinterpret_cast<LPMEASUREITEMSTRUCT>(lParam);
        if (mis->CtlType == ODT_LISTBOX) {
            mis->itemHeight = 24;
        }
        break;
    }

    case WM_DRAWITEM:
    {
        auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (dis->CtlType == ODT_BUTTON) {
            bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
            bool disabled = (dis->itemState & ODS_DISABLED) != 0;

            COLORREF clrN, clrP;
            switch (dis->CtlID) {
            case IDC_BTN_RUN_BASELINE:   clrN = CLR_BTN_QS; clrP = CLR_BTN_QS_P; break;
            case IDC_BTN_EXPORT_REPORT:  clrN = CLR_BTN_BL; clrP = CLR_BTN_BL_P; break;
            case IDC_BTN_CLOSE:          clrN = CLR_BTN_CS; clrP = CLR_BTN_CS_P; break;
            default:                     clrN = CLR_BTN_DIS; clrP = CLR_BTN_DIS; break;
            }
            COLORREF fill = disabled ? CLR_BTN_DIS : (pressed ? clrP : clrN);

            HDC dc  = dis->hDC;
            RECT rc = dis->rcItem;
            HBRUSH hbr = CreateSolidBrush(fill);
            HPEN   hpn = CreatePen(PS_SOLID, 0, fill);
            auto ob = SelectObject(dc, hbr); auto op = SelectObject(dc, hpn);
            RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
            SelectObject(dc, ob); SelectObject(dc, op);
            DeleteObject(hbr); DeleteObject(hpn);

            if (pressed) OffsetRect(&rc, 0, 1);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(255, 255, 255));
            if (g_hFontBtn) SelectObject(dc, g_hFontBtn);
            wchar_t text[64] = {};
            GetWindowTextW(dis->hwndItem, text, 64);
            DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else if (dis->CtlType == ODT_LISTBOX) {
            // Owner-draw listbox items with colored risk level
            HDC dc  = dis->hDC;
            RECT rc = dis->rcItem;

            // Background
            if (dis->itemState & ODS_SELECTED) {
                FillRect(dc, &rc, GetSysColorBrush(COLOR_HIGHLIGHT));
            } else {
                FillRect(dc, &rc, g_hBrushList);
            }

            // Text
            if (dis->itemID != (UINT)-1) {
                wchar_t text[512] = {};
                SendMessageW(dis->hwndItem, LB_GETTEXT, dis->itemID, (LPARAM)text);

                if (wcslen(text) > 0) {
                    // Try to extract risk level from the format: [风险等级] 检测项名称 | 实际值 | 标准
                    SetBkMode(dc, TRANSPARENT);

                    // Determine color based on item data
                    int riskVal = (int)SendMessageW(dis->hwndItem, LB_GETITEMDATA, dis->itemID, 0);
                    COLORREF itemClr = RiskColorRef((RiskLevel)riskVal);

                    if (g_hFontBtn) SelectObject(dc, g_hFontBtn);

                    rc.left += 4;
                    rc.right -= 4;

                    // If selected, use white text
                    if (dis->itemState & ODS_SELECTED) {
                        SetTextColor(dc, RGB(255, 255, 255));
                    } else {
                        SetTextColor(dc, itemClr);
                    }

                    DrawTextW(dc, text, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                }
            }
        }
        break;
    }

    case WM_APP + 1: // Scan complete
    {
        UpdateList(hWnd);
        break;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);

        if (id == IDC_BTN_RUN_BASELINE) {
            RunChecks(hWnd);

        } else if (id == IDC_BTN_EXPORT_REPORT) {
            ExportReport(hWnd);

        } else if (id == IDC_BTN_CLOSE) {
            DestroyWindow(hWnd);

        } else if (HIWORD(wParam) == LBN_DBLCLK && (HWND)lParam == g_hList) {
            // Double-click list item: show details
            int sel = (int)SendMessageW(g_hList, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR && sel < (int)g_results.size()) {
                auto& r = g_results[sel];
                std::wstring msg;
                msg += L"检测项: " + r.checkName + L"\n";
                msg += L"标准: " + r.standard + L"\n";
                msg += L"实际值: " + (r.actualValue.empty() ? L"无" : r.actualValue) + L"\n";
                msg += L"风险等级: ";
                msg += RiskColor(r.riskLevel);
                msg += L"\n";
                msg += L"合规: ";
                msg += r.compliant ? L"是" : L"否";
                msg += L"\n";
                msg += L"修复建议: " + (r.remediation.empty() ? L"无" : r.remediation) + L"\n";
                if (!r.errorMsg.empty())
                    msg += L"错误信息: " + r.errorMsg;
                MessageBoxW(hWnd, msg.c_str(), L"检测项详情", MB_OK | MB_ICONINFORMATION);
            }
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        if (g_hBrushList) { DeleteObject(g_hBrushList); g_hBrushList = nullptr; }
        if (g_hFontTitle) { DeleteObject(g_hFontTitle); g_hFontTitle = nullptr; }
        if (g_hFontBtn)   { DeleteObject(g_hFontBtn);   g_hFontBtn   = nullptr; }
        if (g_hFontMono)  { DeleteObject(g_hFontMono);  g_hFontMono  = nullptr; }
        g_hList = nullptr;
        g_hBtnRun = nullptr;
        g_hBtnExport = nullptr;
        g_hBtnClose = nullptr;
        g_results.clear();
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}