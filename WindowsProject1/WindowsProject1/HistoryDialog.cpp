#include "HistoryDialog.h"
#include "WindowsProject1.h"
#include "resource.h"
#include <commctrl.h>

// 外部实例句柄
extern HINSTANCE hInst;

HWND HistoryDialog::m_hListCtrl = nullptr;

// ---------------------------------------------------------------------------
// 显示历史记录对话框
// ---------------------------------------------------------------------------

void HistoryDialog::Show(HWND hParent)
{
    DialogBox(hInst, MAKEINTRESOURCE(IDD_HISTORY_DIALOG), hParent, DialogProc);
}

// ---------------------------------------------------------------------------
// 对话框过程函数
// ---------------------------------------------------------------------------

INT_PTR CALLBACK HistoryDialog::DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_INITDIALOG:
        return OnInitDialog(hDlg);

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;

        case IDC_BTN_VIEW_DETAILS:
        {
            // 获取选中的项
            int selected = ListView_GetNextItem(m_hListCtrl, -1, LVNI_SELECTED);
            if (selected != -1)
            {
                LVITEM item = {};
                item.mask = LVIF_PARAM;
                item.iItem = selected;
                if (ListView_GetItem(m_hListCtrl, &item))
                {
                    ShowRecordDetails(hDlg, static_cast<int>(item.lParam));
                }
            }
            return TRUE;
        }

        case IDC_BTN_DELETE_RECORD:
            DeleteSelectedRecord(hDlg);
            return TRUE;

        case IDC_BTN_CLEAR_ALL:
            ClearAllRecords(hDlg);
            return TRUE;
        }
        break;

    case WM_NOTIFY:
    {
        NMHDR* pNMHDR = reinterpret_cast<NMHDR*>(lParam);
        if (pNMHDR->hwndFrom == m_hListCtrl && pNMHDR->code == NM_DBLCLK)
        {
            // 双击查看详情
            NMITEMACTIVATE* pNMItemActivate = reinterpret_cast<NMITEMACTIVATE*>(lParam);
            if (pNMItemActivate->iItem != -1)
            {
                LVITEM item = {};
                item.mask = LVIF_PARAM;
                item.iItem = pNMItemActivate->iItem;
                if (ListView_GetItem(m_hListCtrl, &item))
                {
                    ShowRecordDetails(hDlg, static_cast<int>(item.lParam));
                }
            }
        }
        return TRUE;
    }

    case WM_CLOSE:
        EndDialog(hDlg, 0);
        return TRUE;
    }

    return FALSE;
}

// ---------------------------------------------------------------------------
// 初始化对话框
// ---------------------------------------------------------------------------

BOOL HistoryDialog::OnInitDialog(HWND hDlg)
{
    // 设置窗口标题
    SetWindowText(hDlg, GetLocalizedString(IDS_HISTORY_TITLE));

    // 设置按钮文本
    SetDlgItemText(hDlg, IDC_BTN_VIEW_DETAILS, GetLocalizedString(IDS_VIEW_DETAILS));
    SetDlgItemText(hDlg, IDC_BTN_DELETE_RECORD, GetLocalizedString(IDS_DELETE_RECORD));
    SetDlgItemText(hDlg, IDC_BTN_CLEAR_ALL, GetLocalizedString(IDS_CLEAR_ALL));
    SetDlgItemText(hDlg, IDCANCEL, GetLocalizedString(IDS_CLOSE));

    // 获取列表控件
    m_hListCtrl = GetDlgItem(hDlg, IDC_SCAN_HISTORY_LIST);

    // 初始化列表控件
    ListView_SetExtendedListViewStyle(m_hListCtrl,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    // 添加列
    LVCOLUMN column = {};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    column.pszText = const_cast<wchar_t*>(GetLocalizedString(IDS_COL_ID));
    column.cx = 60;
    column.iSubItem = 0;
    ListView_InsertColumn(m_hListCtrl, 0, &column);

    column.pszText = const_cast<wchar_t*>(GetLocalizedString(IDS_COL_TIME));
    column.cx = 150;
    column.iSubItem = 1;
    ListView_InsertColumn(m_hListCtrl, 1, &column);

    column.pszText = const_cast<wchar_t*>(GetLocalizedString(IDS_COL_TYPE));
    column.cx = 100;
    column.iSubItem = 2;
    ListView_InsertColumn(m_hListCtrl, 2, &column);

    column.pszText = const_cast<wchar_t*>(GetLocalizedString(IDS_COL_TOTAL));
    column.cx = 80;
    column.iSubItem = 3;
    ListView_InsertColumn(m_hListCtrl, 3, &column);

    column.pszText = const_cast<wchar_t*>(GetLocalizedString(IDS_COL_THREATS));
    column.cx = 80;
    column.iSubItem = 4;
    ListView_InsertColumn(m_hListCtrl, 4, &column);

    column.pszText = const_cast<wchar_t*>(GetLocalizedString(IDS_COL_SAFE));
    column.cx = 80;
    column.iSubItem = 5;
    ListView_InsertColumn(m_hListCtrl, 5, &column);

    // 刷新记录列表
    RefreshRecordList(hDlg);

    return TRUE;
}

// ---------------------------------------------------------------------------
// 刷新记录列表
// ---------------------------------------------------------------------------

void HistoryDialog::RefreshRecordList(HWND hDlg)
{
    if (!m_hListCtrl) return;

    // 清空列表
    ListView_DeleteAllItems(m_hListCtrl);

    // 获取所有记录
    const auto& records = ScanHistory::Instance().GetAllRecords();

    // 添加记录到列表
    LVITEM item = {};
    item.mask = LVIF_TEXT | LVIF_PARAM;

    int index = 0;
    for (const auto& record : records)
    {
        // ID
        item.iItem = index;
        item.iSubItem = 0;
        item.pszText = const_cast<wchar_t*>(std::to_wstring(record.id).c_str());
        item.lParam = record.id;
        ListView_InsertItem(m_hListCtrl, &item);

        // 时间
        ListView_SetItemText(m_hListCtrl, index, 1, const_cast<wchar_t*>(record.scanTime.c_str()));

        // 类型
        ListView_SetItemText(m_hListCtrl, index, 2, const_cast<wchar_t*>(record.scanType.c_str()));

        // 总数
        ListView_SetItemText(m_hListCtrl, index, 3, const_cast<wchar_t*>(std::to_wstring(record.totalFiles).c_str()));

        // 威胁数
        ListView_SetItemText(m_hListCtrl, index, 4, const_cast<wchar_t*>(std::to_wstring(record.blackFiles).c_str()));

        // 安全数
        ListView_SetItemText(m_hListCtrl, index, 5, const_cast<wchar_t*>(std::to_wstring(record.whiteFiles).c_str()));

        index++;
    }

    // 更新状态
    if (records.empty())
    {
        SetDlgItemText(hDlg, IDC_STATUS, GetLocalizedString(IDS_NO_HISTORY));
    }
    else
    {
        wchar_t status[256];
        swprintf_s(status, L"%s: %d", GetLocalizedString(IDS_TOTAL_RECORDS), static_cast<int>(records.size()));
        SetDlgItemText(hDlg, IDC_STATUS, status);
    }
}

// ---------------------------------------------------------------------------
// 显示选中的记录详情
// ---------------------------------------------------------------------------

void HistoryDialog::ShowRecordDetails(HWND hDlg, int recordId)
{
    const ScanRecord* record = ScanHistory::Instance().GetRecord(recordId);
    if (!record) return;

    // 构建详细信息字符串
    std::wstring details;
    details += GetLocalizedString(IDS_DETAILS_TIME);
    details += record->scanTime;
    details += L"\n\n";

    details += GetLocalizedString(IDS_DETAILS_TYPE);
    details += record->scanType;
    details += L"\n\n";

    details += GetLocalizedString(IDS_DETAILS_TOTAL);
    details += std::to_wstring(record->totalFiles);
    details += L"\n";

    details += GetLocalizedString(IDS_DETAILS_THREATS);
    details += std::to_wstring(record->blackFiles);
    details += L"\n";

    details += GetLocalizedString(IDS_DETAILS_SAFE);
    details += std::to_wstring(record->whiteFiles);
    details += L"\n";

    details += GetLocalizedString(IDS_DETAILS_UNKNOWN);
    details += std::to_wstring(record->unknownFiles);
    details += L"\n";

    details += GetLocalizedString(IDS_DETAILS_ERROR);
    details += std::to_wstring(record->errorFiles);
    details += L"\n";

    details += GetLocalizedString(IDS_DETAILS_HEURISTIC);
    details += std::to_wstring(record->heuristicHits);
    details += L"\n\n";

    if (!record->threatList.empty())
    {
        details += GetLocalizedString(IDS_DETAILS_THREAT_FILES);
        details += L"\n";
        for (const auto& threat : record->threatList)
        {
            details += L"• ";
            details += threat;
            details += L"\n";
        }
    }

    // 显示详情对话框
    MessageBox(hDlg, details.c_str(), GetLocalizedString(IDS_RECORD_DETAILS), MB_OK | MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// 删除选中的记录
// ---------------------------------------------------------------------------

void HistoryDialog::DeleteSelectedRecord(HWND hDlg)
{
    int selected = ListView_GetNextItem(m_hListCtrl, -1, LVNI_SELECTED);
    if (selected == -1)
    {
        MessageBox(hDlg, GetLocalizedString(IDS_SELECT_RECORD), GetLocalizedString(IDS_INFO), MB_OK | MB_ICONINFORMATION);
        return;
    }

    LVITEM item = {};
    item.mask = LVIF_PARAM;
    item.iItem = selected;
    if (!ListView_GetItem(m_hListCtrl, &item))
        return;

    int recordId = static_cast<int>(item.lParam);

    // 确认删除
    if (MessageBox(hDlg, GetLocalizedString(IDS_CONFIRM_DELETE), GetLocalizedString(IDS_CONFIRM), MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    // 删除记录
    if (ScanHistory::Instance().DeleteRecord(recordId))
    {
        RefreshRecordList(hDlg);
        MessageBox(hDlg, GetLocalizedString(IDS_DELETE_SUCCESS), GetLocalizedString(IDS_INFO), MB_OK | MB_ICONINFORMATION);
    }
    else
    {
        MessageBox(hDlg, GetLocalizedString(IDS_DELETE_FAILED), GetLocalizedString(IDS_ERROR), MB_OK | MB_ICONERROR);
    }
}

// ---------------------------------------------------------------------------
// 清空所有记录
// ---------------------------------------------------------------------------

void HistoryDialog::ClearAllRecords(HWND hDlg)
{
    const auto& records = ScanHistory::Instance().GetAllRecords();
    if (records.empty())
    {
        MessageBox(hDlg, GetLocalizedString(IDS_NO_HISTORY), GetLocalizedString(IDS_INFO), MB_OK | MB_ICONINFORMATION);
        return;
    }

    // 确认清空
    if (MessageBox(hDlg, GetLocalizedString(IDS_CONFIRM_CLEAR), GetLocalizedString(IDS_CONFIRM), MB_YESNO | MB_ICONWARNING) != IDYES)
        return;

    // 清空记录
    ScanHistory::Instance().ClearAll();
    RefreshRecordList(hDlg);
    MessageBox(hDlg, GetLocalizedString(IDS_CLEAR_SUCCESS), GetLocalizedString(IDS_INFO), MB_OK | MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// 获取多语言字符串
// ---------------------------------------------------------------------------

const wchar_t* HistoryDialog::GetLocalizedString(int id)
{
    static wchar_t buffer[256];
    LoadStringW(hInst, id, buffer, 256);
    return buffer;
}
