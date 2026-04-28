# PDF Generator Skill - Usage Guide

## Quick Start

```python
from pdf_generator import generate_pdf

# Basic usage
generate_pdf(
    output_pdf="my_report.pdf",
    title="My Business Report",
    author="Your Name"
)
```

## Installation

```bash
pip install reportlab>=4.0.0
```

## Core Features

### 1. Color Themes
- `business_blue` - Professional blue (#3498db)
- `nature_green` - Eco-friendly green (#27ae60)
- `energy_orange` - Energetic orange (#e67e22)
- `corporate_purple` - Corporate purple (#8e44ad)

### 2. Table Styling
```python
data = [
    ["指标", "数据", "时间"],
    ["2024 年出货量", "5.3 GWh", "EVTank"],
]

generate_pdf(
    output_pdf="report.pdf",
    data_tables=[data],
    theme="business_blue"
)
```

### 3. Chinese Font Support
Automatically detects and uses:
- SimHei (黑体)
- Microsoft YaHei (微软雅黑)
- SimSun (宋体)

### 4. Page Management
- Automatic table of contents
- Page breaks between sections
- Custom styling per section

## Advanced Examples

### Multi-Table Report
```python
tables = [
    # Table 1: Market Data
    [["Year", "Market Size", "Growth"],
     [2024, "5.3 GWh", "+12%"]],
    
    # Table 2: Company Timeline
    [["Company", "Milestone", "Year"],
     ["CATL", "R&D Start", "2023"],
     ["BYD", "Production", "2027"]]
]

generate_pdf(
    output_pdf="industry_report.pdf",
    title="Battery Industry Report",
    data_tables=tables,
    include_toc=True
)
```

### With Email Integration
```python
from email_sender import send_email

pdf_path = generate_pdf(
    output_pdf="quarterly_report.pdf",
    title="Q1 2024 Report"
)

send_email(
    to="stakeholders@company.com",
    subject="Q1 Quarterly Report Attached",
    attachment=pdf_path
)
```

## Pre-built Templates

### Executive Summary
```python
from templates.prebuilt_templates import executive_summary_template

executive_summary_template(
    title="Q1 Executive Summary",
    key_findings=[
        "Revenue increased 15% YoY",
        "New customer acquisition up 23%"
    ],
    recommendations=[
        "Expand marketing budget by 20%",
        "Invest in customer retention programs"
    ]
)
```

### Research Paper
```python
from templates.prebuilt_templates import research_paper_template

research_paper_template(
    title="Solid-State Battery Technology Review",
    abstract="This paper examines commercialization progress...",
    sections=[
        ("Introduction", "Background on solid-state batteries..."),
        ("Methodology", "Data collection methods...")
    ]
)
```

## CLI Usage

```bash
# Generate basic report
python pdf_generator.py --input data.md --output report.pdf

# Custom styling
python pdf_generator.py --title "Annual Report" --theme green --output annual.pdf

# Batch processing
for md in reports/*.md; do
    python pdf_generator.py --input "$md" --output "${md%.md}.pdf"
done
```

## Troubleshooting

### Font Not Found
```
Warning: Could not load Chinese font
```
**Solution**: Ensure Windows fonts are available or use English text.

### Permission Denied
```
IOError: Failed to write PDF
```
**Solution**: Check write permissions on output directory.

### Missing Dependencies
```
ModuleNotFoundError: No module named 'reportlab'
```
**Solution**: Install with `pip install reportlab>=4.0.0`.

## Best Practices

1. **Always test locally first** before production deployment
2. **Use consistent themes** across multiple reports
3. **Validate input data** before generating tables
4. **Archive PDFs** after distribution for version control

## License

MIT License - Free for commercial and personal use.
