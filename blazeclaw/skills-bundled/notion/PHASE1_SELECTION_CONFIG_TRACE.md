# Notion Phase 1 Diagnostics (Selection -> Config Route)

## Observation Summary
- Skill Browser selection routes through `CSkillView::NotifySelectionToChatView()`, then opens a tab using `CMainFrame::OpenSkillViewTab()`.
- Skill payload is passed into `CBlazeClawMFCView::ShowSkillSelection()`, which triggers config opening via `OpenSkillConfigDocument()`.
- Config routing outcome is now explicitly logged as:
  - `skills.config.route ... mode=dedicated`
  - `skills.config.route ... mode=generated`

## Reproduced Failing Branch (Before Phase 2)
- Notion was discoverable as a bundled skill under `blazeclaw/skills-bundled/notion/config.html`.
- Config path resolution searched only `blazeclaw/skills/<skill>/config.html`, so Notion dedicated page was missed.
- The flow fell back to generated config page behavior, which depended on payload metadata quality.

## Root Cause Confirmed
- **Primary issue:** resolver root mismatch for bundled skills (`skills-bundled` not in lookup order).
- **Secondary risk:** payload quality varied by Skill Browser category, so generated fallback could degrade.

## Deterministic Route Captured
1. `CSkillView` node selection
2. `CMainFrame::OpenSkillViewTab()`
3. `CBlazeClawMFCView::ShowSkillSelection()`
4. `CBlazeClawMFCView::OpenSkillConfigDocument()`
5. dedicated `config.html` route or generated fallback

