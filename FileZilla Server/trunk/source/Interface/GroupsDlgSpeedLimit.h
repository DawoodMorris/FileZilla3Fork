// FileZilla Server - a Windows ftp server

// Copyright (C) 2002-2016 - Tim Kosse <tim.kosse@filezilla-project.org>

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#if !defined(AFX_GROUPSDLGSPEEDLIMIT_H__C47564E9_A44A_4103_A810_109ECD6215F8__INCLUDED_)
#define AFX_GROUPSDLGSPEEDLIMIT_H__C47564E9_A44A_4103_A810_109ECD6215F8__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// GroupsDlgSpeedLimit.h : Header-Datei
//

/////////////////////////////////////////////////////////////////////////////
// Dialogfeld CGroupsDlgSpeedLimit
#include "GroupsDlg.h"

class CGroupsDlgSpeedLimit : public CSAPrefsSubDlg
{
// Konstruktion
public:
	CGroupsDlgSpeedLimit(CGroupsDlg *pOwner);   // Standardkonstruktor
	~CGroupsDlgSpeedLimit();

	void SetCtrlState();
	BOOL DisplayGroup(const t_group *pGroup);
	BOOL SaveGroup(t_group *pGroup);
	CString Validate();

protected:
	void ShowSpeedLimit( CListBox &listBox, SPEEDLIMITSLIST &list);

	SPEEDLIMITSLIST m_DownloadSpeedLimits;
	SPEEDLIMITSLIST m_UploadSpeedLimits;

	CGroupsDlg *m_pOwner;

// Dialogfelddaten
	//{{AFX_DATA(CGroupsDlgSpeedLimit)
	enum { IDD = IDD_GROUPS_SPEEDLIMIT };
	CButton	m_DownloadUpCtrl;
	CListBox	m_DownloadRulesListCtrl;
	CButton	m_DownloadRemoveCtrl;
	CButton	m_DownloadDownCtrl;
	CButton	m_DownloadAddCtrl;
	CButton	m_UploadUpCtrl;
	CListBox	m_UploadRulesListCtrl;
	CButton	m_UploadRemoveCtrl;
	CButton	m_UploadDownCtrl;
	CButton	m_UploadAddCtrl;
	CEdit	m_UploadValueCtrl;
	CEdit	m_DownloadValueCtrl;
	int		m_DownloadSpeedLimitType;
	int		m_UploadSpeedLimitType;
	int		m_DownloadValue;
	int		m_UploadValue;
	//}}AFX_DATA


// ?berschreibungen
	// Vom Klassen-Assistenten generierte virtuelle Funktions?berschreibungen
	//{{AFX_VIRTUAL(CGroupsDlgSpeedLimit)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV-Unterst?tzung
	//}}AFX_VIRTUAL

// Implementierung
protected:

	// Generierte Nachrichtenzuordnungsfunktionen
	//{{AFX_MSG(CGroupsDlgSpeedLimit)
	virtual BOOL OnInitDialog();
	afx_msg void OnRadio();
	afx_msg void OnSpeedlimitDownloadAdd();
	afx_msg void OnSpeedlimitDownloadRemove();
	afx_msg void OnSpeedlimitDownloadUp();
	afx_msg void OnSpeedlimitDownloadDown();
	afx_msg void OnDblclkSpeedlimitDownloadRulesList();
	afx_msg void OnSpeedlimitUploadAdd();
	afx_msg void OnSpeedlimitUploadRemove();
	afx_msg void OnSpeedlimitUploadUp();
	afx_msg void OnSpeedlimitUploadDown();
	afx_msg void OnDblclkSpeedlimitUploadRulesList();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ f?gt unmittelbar vor der vorhergehenden Zeile zus?tzliche Deklarationen ein.

#endif // AFX_GROUPSDLGSPEEDLIMIT_H__C47564E9_A44A_4103_A810_109ECD6215F8__INCLUDED_
