# comic-card — Comic Strip Card (Paginated)

## File Structure
```
comic-card/
  index.html       — Zero-logic entry page
  ai-card.js       — Web Component: unit tabs, pagination, swipe/keyboard nav, page dots
  ai-card.css      — All styles: tabs, bubbles, dots, transitions, 5-device responsive
  data.json        — 6 PEP外研版 units (Units 1-6), 28 comic panels, 6 activity videos
  assets/
    image/         — 28 panel PNGs (unit1-6_panelN.png)
    video/         — 6 activity MP4s (unit1-6_activity1.mp4)
  Educational Card/ — Source materials (original JSON + Unit folders)
  metadata.md      — This file
```

## JSON Schema

| Field | Required | Description |
|-------|----------|-------------|
| `schema_version` | Yes | Fixed `"1.0"` |
| `card_type` | Yes | Fixed `"comic_strip"` |
| `title` | Yes | Card title |
| `subtitle` | No | Subtitle below title |
| `description` | No | Brief description above the comic |
| `button_text` | No | Not rendered (nav buttons replace it) |
| `video_url` | Yes | Video source for `<video controls>` |
| `theme` | No | Defaults to `"comic"` |
| `frames` | Yes | Array of frame objects |
| `frames[].image` | Yes | Frame image URL |
| `frames[].texts` | Yes | Array of dialogue strings (0-3 per frame) |
| `layout.variant` | No | Fixed `"comic_strip"` |
| `layout.icon` | No | Icon key, defaults to `"comic"` |

## DOM Tree
```
ai-card (Shadow DOM)
  └── .comic-card
        ├── .unit-tabs (6 tabs, one per unit — each color-coded)
        │     └── .unit-tab[data-unit="N"]
        │           ├── .unit-tab-label ("Unit N")
        │           └── .unit-tab-title (topic name)
        ├── .video-area
        │     └── <video controls src="...">
        └── .card-body
              ├── .hdr (title + subtitle)
              ├── .desc (description, optional)
              ├── .comic-viewport
              │     ├── .page-indicator ("1 / 6")
              │     ├── .frame-img (single image)
              │     └── .bubbles
              │           ├── .bubble.left
              │           ├── .bubble.right
              │           └── .bubble.center
              ├── .page-dots
              │     └── .page-dot[data-dot="N"] (.active = current)
              └── .nav
                    ├── .nav-btn[data-nav="prev"] (disabled on first page)
                    └── .nav-btn.primary[data-nav="next"] (disabled on last page)
```

## Speech Bubble Rules
- texts[0] → `.bubble.left` (blue, top-left)
- texts[1] → `.bubble.right` (pink, top-right; moves to bottom-right when only 2 texts)
- texts[2+] → `.bubble.center` (green, bottom-center, bold)
- Bubbles animate in with a staggered pop effect

## Unit Tabs
- 6 color-coded tabs (amber, blue, green, pink, purple, orange)
- Horizontally scrollable on narrow screens
- Clicking a tab loads that unit (video + panels reset to page 1)
- Active tab has filled background with the unit's brand color

## Navigation
- **Prev/Next buttons**: Navigate between panels within a unit
- **Page dots**: Click any dot to jump to that panel; active dot is elongated
- **Touch swipe**: Swipe left/right on the image viewport to navigate
- **Keyboard**: Left/Right arrow keys navigate between panels
- **Slide transitions**: Directional slide animation when navigating between panels

## Responsive Breakpoints

| Device | Width | Card max-width |
|--------|-------|---------------|
| Phone | < 480px | 380px |
| Tablet | ≥ 480px | 440px |
| Large | ≥ 768px | 520px |
| Desktop | ≥ 1024px | 600px |
| TV | ≥ 1440px | 720px |

## Theme
- Each unit has its own accent color from the palette
- CSS custom properties (`--c-brand`, `--c-light`, `--c-soft`) update on unit switch
- Primary button, active dot, and active tab use the current unit's brand color

## Data: 6 Units (PEP外研版)
| Unit | Topic | Panels | Video |
|------|-------|--------|-------|
| 1 | Pets — Meet My Little Friends | 5 | ✓ |
| 2 | Animals — A Day at the Zoo | 6 | ✓ |
| 3 | Face — We Are Twins! | 6 | ✓ |
| 4 | Body — My Robot Monster | 4 | ✓ |
| 5 | My Home — Tidy Up Time | 6 | ✓ |
| 6 | Time — Happy Birthday, Dad! | 1 | ✓ |
