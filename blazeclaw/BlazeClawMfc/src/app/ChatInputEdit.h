#pragma once

#include "pch.h"

class CChatInputEdit : public CEdit
{
public:
	CWnd* m_pOwner = nullptr; // 期望指向 CChatView

protected:
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	DECLARE_MESSAGE_MAP()
};

