// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2007-2009, 2013 - TortoiseSVN

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
#pragma once
#include "MergeWizardBasePage.h"
#include "Tooltip.h"

/**
 * Last page in the merge wizard for selecting the merge options.
 */
class CMergeWizardOptions : public CMergeWizardBasePage
{
    DECLARE_DYNAMIC(CMergeWizardOptions)

public:
    CMergeWizardOptions();
    virtual ~CMergeWizardOptions();


    enum { IDD = IDD_MERGEWIZARD_OPTIONS };

protected:
    virtual void        DoDataExchange(CDataExchange* pDX);
    virtual BOOL        OnInitDialog();
    virtual LRESULT     OnWizardBack();
    virtual BOOL        OnWizardFinish();
    virtual BOOL        OnSetActive();
    virtual BOOL        PreTranslateMessage(MSG* pMsg);
    afx_msg void        OnBnClickedDryrun();
    afx_msg void        OnBnClickedReintegrateoldstyle();

    DECLARE_MESSAGE_MAP()

    svn_diff_file_ignore_space_t GetIgnores();

    CComboBox                       m_depthCombo;
    CToolTips                       m_tooltips;
};
