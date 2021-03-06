// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2007-2010, 2012-2016 - TortoiseSVN

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
#include "stdafx.h"
#include "MergeWizardBasePage.h"
#include "resource.h"

IMPLEMENT_DYNAMIC(CMergeWizardBasePage, CResizablePageEx)

void CMergeWizardBasePage::SetButtonTexts()
{
    CPropertySheet* psheet = (CPropertySheet*) GetParent();
    if (psheet)
    {
        psheet->GetDlgItem(ID_WIZFINISH)->SetWindowText(CString(MAKEINTRESOURCE(IDS_MERGE_MERGE)));
        psheet->GetDlgItem(ID_WIZBACK)->SetWindowText(CString(MAKEINTRESOURCE(IDS_PROPPAGE_BACK)));
        psheet->GetDlgItem(ID_WIZNEXT)->SetWindowText(CString(MAKEINTRESOURCE(IDS_PROPPAGE_NEXT)));
        psheet->GetDlgItem(IDHELP)->SetWindowText(CString(MAKEINTRESOURCE(IDS_PROPPAGE_HELP)));
        CAppUtils::SetAccProperty(psheet->GetDlgItem(IDHELP)->GetSafeHwnd(), PROPID_ACC_KEYBOARDSHORTCUT, L"F1");
        psheet->GetDlgItem(IDCANCEL)->SetWindowText(CString(MAKEINTRESOURCE(IDS_PROPPAGE_CANCEL)));
        CAppUtils::SetAccProperty(psheet->GetDlgItem(IDCANCEL)->GetSafeHwnd(), PROPID_ACC_KEYBOARDSHORTCUT, L"ESC");
    }
}

void CMergeWizardBasePage::StartWCCheckThread(const CTSVNPath& path)
{
    m_path = path;
    m_pThread = AfxBeginThread(FindRevThreadEntry, this);
}

void CMergeWizardBasePage::StopWCCheckThread()
{
    m_bCancelled = true;
    if ((m_pThread)&&(m_bThreadRunning))
    {
        WaitForSingleObject(m_pThread->m_hThread, 1000);
        if (m_bThreadRunning)
        {
            // we gave the thread a chance to quit. Since the thread didn't
            // listen to us we have to kill it.
            TerminateThread(m_pThread->m_hThread, (DWORD)-1);
            InterlockedExchange(&m_bThreadRunning, FALSE);
        }
    }
}

UINT CMergeWizardBasePage::FindRevThreadEntry(LPVOID pVoid)
{
    CCrashReportThread crashthread;
    return ((CMergeWizardBasePage*)pVoid)->FindRevThread();
}

UINT CMergeWizardBasePage::FindRevThread()
{
    svn_revnum_t    minrev;
    svn_revnum_t    maxrev;
    bool            bswitched;
    bool            bmodified;
    bool            bSparse;

    InterlockedExchange(&m_bThreadRunning, TRUE);
    if (GetWCRevisionStatus(m_path, true, minrev, maxrev, bswitched, bmodified, bSparse))
    {
        if (!m_bCancelled)
            SendMessage(WM_TSVN_MAXREVFOUND, (WPARAM)bmodified);
    }
    InterlockedExchange(&m_bThreadRunning, FALSE);
    return 0;
}

/**
 * Display a balloon with close button, anchored at a given combo box edit control on this dialog.
 */
void CMergeWizardBasePage::ShowComboBalloon(CComboBoxEx * pCombo, UINT nIdText, UINT nIdTitle, int nIcon /* = TTI_WARNING */)
{
    CString text(MAKEINTRESOURCE(nIdText));
    CString title(MAKEINTRESOURCE(nIdTitle));
    EDITBALLOONTIP bt;
    bt.cbStruct = sizeof(bt);
    bt.pszText  = text;
    bt.pszTitle = title;
    bt.ttiIcon = nIcon;
    HWND hEdit = pCombo->GetEditCtrl()->GetSafeHwnd();
    ::SendMessage(hEdit, EM_SHOWBALLOONTIP, 0, (LPARAM)&bt);
}
