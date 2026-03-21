#include "pch.h"
#include "ChatInputEdit.h"

BEGIN_MESSAGE_MAP(CChatInputEdit, CEdit)
	ON_WM_KEYDOWN()
END_MESSAGE_MAP()

void CChatInputEdit::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (nChar == VK_RETURN)
	{
		const bool bShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		if (bShift)
		{
			CEdit::OnKeyDown(nChar, nRepCnt, nFlags);
			return;
		}

		if (m_pOwner != nullptr)
		{
			m_pOwner->SendMessage(WM_COMMAND, MAKEWPARAM(1001, BN_CLICKED), 0);
		}
		return;
	}

	CEdit::OnKeyDown(nChar, nRepCnt, nFlags);
}

