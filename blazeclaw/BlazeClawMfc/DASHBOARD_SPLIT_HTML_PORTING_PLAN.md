# Dashboard-Specific HTML Porting Plan

## Goal

Split the current shared dashboard WebView entry page into dashboard-specific HTML entry pages so each `CMainFrame` dashboard pane can load a page that matches its own dashboard identity.

The first target is the cron dashboard:

- Current shared page: `blazeclaw/BlazeClawMfc/web/chat/dashboard.html`
- Planned cron page: `blazeclaw/BlazeClawMfc/web/chat/dashboard_cron.html`
- Target pane: `CMainFrame::m_wndDashboard_cron`
- Target behavior: `m_wndDashboard_cron` loads `dashboard_cron.html` directly.
- Extraction behavior: `dashboard_cron.html` represents the UI produced when the `cron` dashboard tag is active, with all other dashboard tags removed.

## Current State Summary

### C++ Dashboard Panes

`CMainFrame` currently declares one legacy dashboard pane and eleven named dashboard panes:

- `m_wndDashboard`
- `m_wndDashboard_overview`
- `m_wndDashboard_tools`
- `m_wndDashboard_files`
- `m_wndDashboard_skills`
- `m_wndDashboard_channels`
- `m_wndDashboard_cron`
- `m_wndDashboard_dreaming`
- `m_wndDashboard_nodes`
- `m_wndDashboard_instances`
- `m_wndDashboard_usage`
- `m_wndDashboard_devices`

These panes are all instances of `CDashboardWnd`, and each pane currently uses the same URL resolution path:

- `CDashboardWnd::ResolveDashboardNavigationUrl()`
- `ResolveDashboardStartupUrl()`
- `blazeclaw::app::chatui::FindDashboardUiIndex()`

`FindDashboardUiIndex()` currently searches only for `dashboard.html` under source and dist roots.

### Current Dashboard HTML Shell

`web/chat/dashboard.html` is a small shell page. It does not contain static markup for every dashboard tag. Instead, it provides shared host elements:

- Header/status elements:
  - `#assistantIdentity`
  - `#speechStatus`
  - `#status`
- Dashboard host elements:
  - `#agentsControlPlane`
  - `#agentsTabs`
  - `#agentsSurface`
- Hidden chat and bridge compatibility elements:
  - `#approvalQueue`
  - `#messages`
  - `#detachedNotices`
  - `#speechLivePreview`
  - hidden composer controls
  - hidden session controls
  - hidden file input

The dashboard-specific visible UI is rendered dynamically by `index.js` into `#agentsTabs` and `#agentsSurface`.

### Current JavaScript Loading Difference

`index.html` loads the full dashboard controller set:

- `controller-utils.js`
- `channels-controller.js`
- `dreaming-controller.js`
- `usage-controller.js`
- `cron-controller.js`
- `files-controller.js`
- `skills-controller.js`
- `tools-controller.js`
- `nodes-controller.js`
- `instances-controller.js`
- `observability-controller.js`
- `devices-controller.js`
- `agents-controller.js`
- chat bridge scripts
- `index.js`

`dashboard.html` currently loads fewer controller modules:

- `scope-errors.js`
- `channels-state-contract.js`
- `config-form-coerce.js`
- `config-form-utils.js`
- `agents-controller.js`
- chat bridge scripts
- `index.js`

This difference is important. `agents-controller.js` can create optional panel runtimes only when the corresponding module exists on `window`, for example `window.BlazeClawCronController`. If `dashboard_cron.html` is expected to use `cron-controller.js`, that script should be loaded explicitly before `agents-controller.js`.

## Dashboard HTML and JavaScript Architecture

### Dynamic Tab Rendering

`index.js` renders dashboard tabs in `renderAgentsTabs()`.

The current tab list includes:

- `overview`
- `tools`
- `files`
- `skills`
- `channels`
- `cron`
- `dreaming`
- `nodes`
- `instances`
- `usage`
- optional `observability`
- `devices`

The active tab is stored in `state.agentsPanel`.

When a tab is clicked:

- `agentsController.setAgentsPanel(panel)` is called.
- The selected panel is persisted.
- Telemetry is emitted.
- `agentsController.loadPanelDataForCurrentAgent()` loads the panel data.

### Dynamic Panel Rendering

`index.js` renders panel content in `renderAgentsSurface()`.

For the cron panel, the branch is:

- `if (panel === "cron") { ... }`

The cron branch renders:

- cron status rows
- job count and run count rows
- selected job state
- job chips using `data-cron-job-id`
- cron job create/edit form fields using `data-cron-field`
- mutation and pagination actions using `data-cron-action`
- `datalist#cronModelSuggestions`
- parity and capability notes

After rendering, `index.js` attaches cron event handlers to:

- `[data-cron-job-id]`
- `[data-cron-field]`
- `[data-cron-action]`

### Cron Controller Responsibilities

`cron-controller.js` defines `window.BlazeClawCronController.createCronController()`.

Its runtime handles:

- `models.list` for model suggestions
- `cron.status`
- `cron.list`
- `cron.runs`
- `cron.add`
- `cron.update`
- `cron.remove`
- `cron.run`
- `wake`
- cron form field updates
- job edit, clone, cancel-edit workflows
- pagination and filtering

`agents-controller.js` delegates cron behavior to this runtime when `window.BlazeClawCronController` is available.

### Panel Dispatch in `agents-controller.js`

`agents-controller.js` validates and normalizes `state.agentsPanel` against a supported panel list.

`loadPanelDataForCurrentAgent()` dispatches by `state.agentsPanel`:

- `cron` calls `loadAgentCron(selectedAgentId)` and does not require a selected agent.
- `tools`, `files`, `skills`, `channels`, and `dreaming` require a selected agent.
- `nodes`, `instances`, `usage`, `observability`, and `devices` load global/system surfaces.
- `overview` loads selected agent identity.

For a dashboard-specific page, the page should set the desired initial panel before the first tab render and before the first panel data load.

## Recommended Extraction Strategy

### Prefer Dashboard Page Identity Over Static DOM Extraction

Although the desired behavior can be described as extracting `dashboard_cron.html` from `dashboard.html` when the `cron` tag is clicked, the current page does not contain static per-tag markup. The practical extraction should therefore be implemented as a page identity mechanism:

- Keep a shared shell structure.
- Load only the required dashboard controller scripts for the page.
- Provide a page-level dashboard panel hint such as `window.__BLAZECLAW_DASHBOARD_PANEL__ = "cron"`.
- Teach `index.js` and/or `agents-controller.js` to use that hint as the initial and fixed panel.
- Hide or omit other tabs for fixed-panel pages.

This avoids copying generated runtime HTML and keeps the cron page synchronized with the live cron renderer.

### Proposed `dashboard_cron.html` Shape

`dashboard_cron.html` should keep the required host elements from `dashboard.html`:

- `#status`
- `#agentsControlPlane`
- `#agentsTabs`
- `#agentsSurface`
- bridge compatibility elements used by `index.js`
- hidden composer/session controls unless `index.js` is later split into a dashboard-only bootstrap

The page should load scripts in dependency order:

1. Shared helpers:
   - `scope-errors.js`
   - `channels-state-contract.js`
   - `config-form-coerce.js`
   - `config-form-utils.js`
   - `controller-utils.js` if future cron rendering relies on shared utilities
2. Cron-specific module:
   - `cron-controller.js`
3. Agent orchestration:
   - `agents-controller.js`
4. Bridge/chat compatibility modules:
   - `chat-controller.js`
   - `chat-events.js`
   - `chat-composer.js`
5. Bootstrap:
   - `index.js`

The page should set identity before loading `index.js`:

- host marker: dashboard host mode
- desired panel: `cron`
- optional fixed-panel flag: no other tags/tabs should be rendered

### Do Not Copy Rendered Cron HTML as Static Markup

Avoid copying the current rendered cron panel HTML into `dashboard_cron.html` as a static snapshot because:

- Cron rows depend on gateway state and async bridge responses.
- Form fields are bound after each render.
- Pagination, selected job state, model suggestions, and run filters live in JavaScript state.
- Static copied markup would drift from `renderAgentsSurface()` quickly.

Instead, use the same renderer with a fixed initial panel.

## Recommended C++ Routing Strategy

### Add Dashboard Identity to `CDashboardWnd`

`CDashboardWnd` needs a flexible identity field rather than relying on separate classes or hard-coded function paths for each pane.

Recommended model:

- Add a small dashboard page identity value to `CDashboardWnd`, for example:
  - default/empty: `dashboard.html`
  - `overview`: `dashboard_overview.html`
  - `tools`: `dashboard_tools.html`
  - `files`: `dashboard_files.html`
  - `skills`: `dashboard_skills.html`
  - `channels`: `dashboard_channels.html`
  - `cron`: `dashboard_cron.html`
  - `dreaming`: `dashboard_dreaming.html`
  - `nodes`: `dashboard_nodes.html`
  - `instances`: `dashboard_instances.html`
  - `usage`: `dashboard_usage.html`
  - `devices`: `dashboard_devices.html`

Then `MainFrame.cpp` can assign the identity when creating or initializing each pane.

### Generalize Dashboard Startup Resolution

Instead of `FindDashboardUiIndex()` always searching for `dashboard.html`, provide a generalized dashboard entry resolver that accepts a file name or panel id.

Recommended resolution order:

1. `BLAZECLAW_DASHBOARD_DEV_URL`
   - Append a panel query parameter for panel-specific panes, for example `?host=dashboard&panel=cron`.
2. `OPENCLAW_UI_DEV_URL`
   - Continue appending `host=dashboard`, and also append panel identity when present.
3. `BLAZECLAW_DASHBOARD_UI_FILE`
   - If explicitly set, use it as an override for all panes unless a new per-panel override is introduced.
4. Optional per-panel override:
   - `BLAZECLAW_DASHBOARD_CRON_UI_FILE`
   - or a generalized environment pattern if needed later.
5. `BLAZECLAW_DASHBOARD_UI_ROOT`
   - Look for the panel-specific file first, then fall back to `dashboard.html`.
6. Source/dist root search:
   - Look for `dashboard_cron.html` first for the cron pane.
   - Fall back to `dashboard.html` with a panel query parameter if the specific file does not exist.

This gives a smooth migration path: panes can be wired before every dashboard-specific HTML file exists.

### Preserve Cache Busting

`CDashboardWnd::ResolveDashboardNavigationUrl()` appends `_wv_refresh=<tick>` today. Keep this behavior for panel-specific URLs.

When adding `panel=cron`, ensure query parameters are composed safely so `_wv_refresh` is still appended exactly once.

## Potential Issues

### Missing Controller Scripts

`dashboard.html` currently does not load `cron-controller.js`. If `dashboard_cron.html` is based directly on `dashboard.html` and only changes the active panel, cron operations may use no-op fallbacks from `agents-controller.js`.

Recommendation:

- Add `cron-controller.js` to `dashboard_cron.html` before `agents-controller.js`.
- Consider aligning `dashboard.html` script loading with `index.html` if the shared dashboard page is expected to support all tabs fully.

### Persistence Can Override the Intended Panel

`index.js` currently initializes `state.agentsPanel = "overview"` and then applies persisted panel state.

For fixed-panel pages, this is risky because a previous persisted panel could override `cron`.

Recommendation:

- Add fixed dashboard panel handling that takes precedence over persisted panel state.
- If `window.__BLAZECLAW_DASHBOARD_PANEL__` is set, do not restore a different persisted panel.
- Persistence should either be disabled for fixed-panel pages or scoped per dashboard page.

### Tabs Are Rendered Dynamically

Removing non-cron tags from `dashboard_cron.html` cannot be done only by editing the HTML file because tabs are generated by `renderAgentsTabs()`.

Recommendation:

- Teach `renderAgentsTabs()` to render only the fixed panel tab when fixed-panel mode is active.
- Alternatively hide `#agentsTabs` entirely for fixed-panel pages.

### Panel Data Loading Depends on `state.agentsPanel`

Cron loading occurs only when `state.agentsPanel === "cron"`.

Recommendation:

- Set the initial panel before creating or applying the agents controller state.
- Ensure the first `agentsController.loadAgents().then(...)` path eventually calls `loadPanelDataForCurrentAgent()` with `state.agentsPanel === "cron"`.

### Shared `index.js` Assumes Chat Compatibility DOM

`index.js` reads many DOM elements used by chat features even in dashboard host mode.

Recommendation:

- Keep the hidden compatibility DOM elements in `dashboard_cron.html` for the first port.
- Defer deeper `index.js` splitting until after page identity routing is stable.

### Multiple WebViews May Duplicate Work

If multiple dashboard panes are visible at the same time, each WebView instance can:

- create an independent bridge controller
- subscribe to lifecycle events
- poll sessions
- poll nodes or observability when active
- load panel data independently

Recommendation:

- Expect duplicated WebView startup work during the first phase.
- Keep per-page controllers lightweight.
- Avoid enabling background polling for fixed pages unless the visible panel requires it.
- Consider visibility-aware refresh/polling later if multiple dashboards are intended to stay open concurrently.

### Query Parameter and File URL Handling

`BuildFileUrl()` escapes a limited set of characters and produces `file://` URLs. Dashboard-specific files should preserve this flow.

Recommendation:

- Do not hand-build `file:///` paths outside the existing helper.
- Keep query appending centralized in `ResolveDashboardNavigationUrl()`.

### Dist Build Parity

`FindDashboardUiEntry()` now checks source and `dist` locations for a preferred dashboard entry file and supports fallback to `dashboard.html`.

Recommendation:

- If dashboard-specific HTML files are source-only during initial development, make source fallback explicit.
- If release packaging depends on `dist`, ensure the dist pipeline copies or generates `dashboard_cron.html`.

### Observability Count Mismatch

The C++ field list has 12 dashboard panes if `m_wndDashboard` is counted with the 11 named panes. The JavaScript tabs also include optional `observability`, but `CMainFrame` currently does not declare `m_wndDashboard_observability`.

Recommendation:

- Clarify whether observability should remain optional inside the shared page or become a separate pane later.
- Do not block the cron split on observability.

## Implementation Phases

### Phase 1: Safe Cron Page Introduction

Status: Completed

Implemented files:

- `blazeclaw/BlazeClawMfc/web/chat/dashboard_cron.html`
- `blazeclaw/BlazeClawMfc/web/chat/index.js`
- `blazeclaw/BlazeClawMfc/src/app/DashboardWnd.h`
- `blazeclaw/BlazeClawMfc/src/app/DashboardWnd.cpp`
- `blazeclaw/BlazeClawMfc/src/app/ChatUiStartupResolver.h`
- `blazeclaw/BlazeClawMfc/src/app/MainFrame.cpp`

- Create `dashboard_cron.html` from the shared shell.
- Add a page-level fixed panel hint for `cron`.
- Load `cron-controller.js` before `agents-controller.js`.
- Keep all hidden compatibility elements from `dashboard.html`.
- Update JavaScript to honor fixed-panel mode.
- Route only `m_wndDashboard_cron` to `dashboard_cron.html`.
- Fall back to `dashboard.html?panel=cron` if `dashboard_cron.html` is missing.

### Phase 2: Generalized Dashboard Page Routing

Status: Completed

Implemented files:

- `blazeclaw/BlazeClawMfc/src/app/MainFrame.cpp`
- `blazeclaw/BlazeClawMfc/src/app/DashboardWnd.cpp`
- `blazeclaw/BlazeClawMfc/src/app/ChatUiStartupResolver.h`

- Add dashboard identity assignment for all named `CDashboardWnd` members.
- Generalize dashboard file lookup to `dashboard_<panel>.html`.
- Keep `dashboard.html` as the shared fallback.
- Document environment variable behavior for dev URLs, explicit files, and UI roots.

Environment variable routing behavior after Phase 2:

- `BLAZECLAW_DASHBOARD_DEV_URL`
  - Highest-priority override URL for all dashboard panes.
  - Pane `panel` hint is appended if configured and not already present.
- `OPENCLAW_UI_DEV_URL`
  - Uses `host=dashboard` suffix and then applies pane `panel` hint when present.
- `BLAZECLAW_DASHBOARD_UI_FILE`
  - Explicit file override for all panes when the file exists.
- `BLAZECLAW_DASHBOARD_UI_ROOT`
  - Resolves `dashboard_<panel>.html` first.
  - Falls back to `dashboard.html` in source root.
  - Then checks `dist/dashboard_<panel>.html` and falls back to `dist/dashboard.html`.
- `BLAZECLAW_CHAT_UI_MODE`
  - `dev` uses `http://127.0.0.1:5173/dashboard.html` as base, then appends pane `panel` hint.
  - `source`/`dist` influence source-vs-dist preference in workspace root scanning.
- Source/dist workspace scanning
  - Uses `FindDashboardUiEntry(...)` for preferred `dashboard_<panel>.html`.
  - Falls back to `dashboard.html` when the preferred page does not exist.

### Phase 3: Extract Additional Dashboard Pages

Create additional pages as needed:

- `dashboard_overview.html`
- `dashboard_tools.html`
- `dashboard_files.html`
- `dashboard_skills.html`
- `dashboard_channels.html`
- `dashboard_dreaming.html`
- `dashboard_nodes.html`
- `dashboard_instances.html`
- `dashboard_usage.html`
- `dashboard_devices.html`

Each page should load only the required controller modules where practical, while preserving shared bridge compatibility during the transition.

### Phase 4: Optional Bootstrap Cleanup

After the page split is stable:

- Extract dashboard-only bootstrap logic from `index.js`.
- Reduce required hidden chat DOM in dashboard-only pages.
- Make polling and lifecycle subscriptions visibility-aware.
- Add small smoke/regression checks for each page identity.

## Recommended Validation Checklist

### File-Level Validation

- `dashboard.html` still loads and shows the shared dashboard.
- `dashboard_cron.html` exists and loads without script errors.
- `dashboard_cron.html` loads `cron-controller.js` before `agents-controller.js`.
- The fixed panel hint is defined before `index.js` runs.

### UI Behavior Validation

- Opening the cron dashboard pane shows cron content immediately.
- Non-cron dashboard tabs are removed or hidden in the cron page.
- Refresh, add/update, edit, clone, run-now, remove, load-more-jobs, and load-more-runs actions are still wired.
- Cron form input updates state correctly.
- Cron jobs and runs load from the gateway.
- Model suggestions load when the bridge is connected.

### C++ Routing Validation

- `m_wndDashboard_cron` navigates to `dashboard_cron.html` when the file exists.
- Missing `dashboard_cron.html` falls back to `dashboard.html` plus a cron panel hint.
- Other dashboard panes keep existing behavior until their specific pages are created.
- `_wv_refresh` cache busting continues to work.

### Build Validation

Use the project validation command specified for BlazeClaw:

- `msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`

## Open Questions

- Should `m_wndDashboard` remain a shared all-dashboard shell, or should it become an alias for overview?
- Should fixed-panel pages show one active tab, no tabs, or a breadcrumb/title only?
- Should dashboard page persistence be global, per page, or disabled for fixed-panel pages?
- Should per-dashboard file routing support per-panel environment overrides immediately?
- Should observability become its own `CDashboardWnd` member in a later change?

## Summary Recommendation

Implement the split as a flexible fixed-panel dashboard entry system rather than as a static copy of rendered HTML. Start with `dashboard_cron.html`, load `cron-controller.js`, set a `cron` page identity before `index.js`, and update the renderer/bootstrap to honor fixed-panel mode. Then add a generalized C++ page identity resolver so each `CDashboardWnd` can load `dashboard_<panel>.html` with safe fallback to the shared `dashboard.html` during incremental porting.
