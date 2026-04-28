# pdf-generator - Professional PDF Report Generator from Markdown

## Description
Convert Markdown documents to professional, well-formatted PDF reports using ReportLab. Supports Chinese fonts, custom styling, tables, and structured layouts. Perfect for business reports, research papers, and data-driven documentation.

## Features
- ✅ Convert Markdown files to A4 format PDF
- ✅ Support Chinese fonts (SimHei/Microsoft YaHei)
- ✅ Custom table styling with themes
- ✅ Multi-page document support
- ✅ Automatic section headers and page breaks
- ✅ Export multiple report formats (summary + detailed)

## Dependencies
- Python 3.8+
- ReportLab >= 4.0 (`pip install reportlab`)

## Usage Examples

### Basic Usage
```python
# Simple markdown to PDF conversion
from pdf_generator import generate_pdf

generate_pdf(
    input_md="report.md",
    output_pdf="report.pdf",
    title="Report Title",
    author="Author Name",
    date="2026-04-28"
)
```

### With Custom Tables
```python
# Generate report with styled tables
data_tables = [
    ["指标", "数据", "时间"],
    ["2024 年全球出货量", "5.3 GWh", "EVTank"],
    ["半固态电池渗透率", "1%", "行业调研"],
]

generate_pdf(
    input_md="battery_report.md",
    output_pdf="battery_report.pdf",
    include_table_styles=True,
    theme="business_blue"
)
```

### CLI Mode
```bash
# From workspace directory
python scripts/pdf-generator.py \
    --input "source.md" \
    --output "report.pdf" \
    --title "My Report" \
    --author "OpenClaw AI Team"
```

## Configuration Options

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `input_md` | str | required | Input Markdown file path |
| `output_pdf` | str | required | Output PDF file path |
| `title` | str | "Untitled Report" | Document title |
| `author` | str | "AI Assistant" | Author name |
| `theme` | str | "business_blue" | Color theme: blue/green/orange/purple |
| `include_toc` | bool | True | Include table of contents |
| `chinese_font` | str | None | Use system Chinese font if available |

## Themed Color Schemes

```python
THEMES = {
    "business_blue":   "#3498db",  # Professional blue
    "nature_green":    "#27ae60",  # Eco-friendly green
    "energy_orange":   "#e67e22",  # Energetic orange
    "corporate_purple":"#8e44ad",  # Corporate purple
}
```

## Advanced Usage

### Custom Table Styles
```python
from pdf_generator import TableStyleBuilder

# Create custom header style
header_style = TableStyleBuilder(
    background="#2c3e50",
    text_color="white",
    font_size=11,
    padding=10
)

# Add to report
table = build_table(data, style=header_style)
```

### Multi-Page with Page Breaks
```python
from reportlab.platypus import PageBreak

story.append(PageBreak())  # Force new page
story.append(Paragraph("Next Section", heading_style))
```

### Extract Data Tables
```python
from pdf_extractor import extract_tables

tables = extract_tables("existing_report.pdf")
for i, table in enumerate(tables):
    print(f"Table {i+1}: {len(table)} rows")
```

## Error Handling

```python
try:
    generate_pdf(input_md="source.md", output_pdf="report.pdf")
except FileNotFoundError:
    print("Input file not found")
except IOError as e:
    print(f"I/O error: {e}")
except Exception as e:
    print(f"Unexpected error: {type(e).__name__}: {e}")
```

## Integration with Other Tools

### Combined with Email Sending
```python
from email_sender import send_email_with_attachment

generate_pdf("report.md", "report.pdf")
send_email(
    to="user@example.com",
    subject="PDF Report Attached",
    attachment="report.pdf"
)
```

### Batch Processing
```python
import glob

for md_file in glob.glob("reports/*.md"):
    pdf_name = md_file.replace(".md", ".pdf")
    generate_pdf(md_file, pdf_name)
    print(f"✓ Converted {md_file}")
```

## File Structure

```
~/.openclaw/skills/pdf-generator/
├── SKILL.md                 # This file
├── README.md                # Extended documentation
├── pdf_generator.py         # Core generator module
├── templates/               # Pre-built report templates
│   ├── business_report.py
│   ├── research_paper.py
│   └── executive_summary.py
├── requirements.txt         # pip dependencies
└── tests/                   # Unit tests
    ├── test_generation.py
    └── test_themes.py
```

## Changelog

### v1.0.0 (2026-04-28)
- Initial release
- Basic markdown to PDF conversion
- 4 color themes
- Chinese font support
- Table styling

## License

MIT License - Free for personal and commercial use.

## Support

For issues or feature requests, contact the OpenClaw team or submit a PR on GitHub.
