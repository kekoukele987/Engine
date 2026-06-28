#pragma once
#include <windows.h>
#include <vector>
#include "ScanHistory.h"

// 历史记录对话框类
class HistoryDialog
{
public:
    // 显示历史记录对话框
    static void Show(HWND hParent);

private:
    HistoryDialog() = default;
    ~HistoryDialog() = default;

    // 对话框过程函数
    static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

    // 初始化对话框
    static BOOL OnInitDialog(HWND hDlg);

    // 刷新记录列表
    static void RefreshRecordList(HWND hDlg);

    // 显示选中的记录详情
    static void ShowRecordDetails(HWND hDlg, int recordId);

    // 删除选中的记录
    static void DeleteSelectedRecord(HWND hDlg);

    // 清空所有记录
    static void ClearAllRecords(HWND hDlg);

    // 获取多语言字符串
    static const wchar_t* GetLocalizedString(int id);

    static HWND m_hListCtrl;  // 列表控件句柄
};