#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
Advanced PDF Generator - Enhanced with full content rendering
Supports complex reports with multiple tables, sections, and custom styling.
"""

from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import inch, cm
from reportlab.platypus import SimpleDocTemplate, Table, TableStyle, Paragraph, Spacer, PageBreak
from reportlab.lib.enums import TA_LEFT, TA_CENTER, TA_JUSTIFY
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
import os


THEMES = {
    "business_blue": "#3498db",
    "nature_green":  "#27ae60",
    "energy_orange": "#e67e22",
    "corporate_purple": "#8e44ad",
}


def load_chinese_font():
    """Load Chinese fonts from Windows system."""
    chinese_fonts = [
        ("C:/Windows/Fonts/simhei.ttf", "SimHei"),
        ("C:/Windows/Fonts/msyh.ttc", "MicrosoftYaHei"),
        ("C:/Windows/Fonts/simsun.ttc", "SimSun"),
    ]
    
    for font_path, name in chinese_fonts:
        if os.path.exists(font_path):
            try:
                pdfmetrics.registerFont(TTFont(name, font_path))
                return name
            except Exception:
                continue
    return None


def parse_markdown_sections(markdown_text):
    """Parse markdown into structured sections."""
    sections = []
    current_section = {"title": "", "content": ""}
    
    lines = markdown_text.split('\n')
    for line in lines:
        if line.startswith('##'):
            if current_section['title']:
                sections.append(current_section)
            current_section = {'title': line.strip(), 'content': ''}
        elif line.strip():
            current_section['content'] += line + '\n'
    
    if current_section['title']:
        sections.append(current_section)
    
    return sections


def build_table_style(table_data, theme_color, chinese_font_name='Chinese'):
    """Build table style with proper alignment and coloring."""
    rows = len(table_data)
    cols = len(table_data[0]) if rows > 0 else 0
    
    styles = [
        ('BACKGROUND', (0, 0), (-1, 0), colors.HexColor(theme_color)),
        ('TEXTCOLOR', (0, 0), (-1, 0), colors.whitesmoke),
        ('ALIGN', (0, 0), (-1, -1), 'LEFT'),
        ('FONTNAME', (0, 0), (-1, 0), chinese_font_name or 'SimHei'),
        ('FONTSIZE', (0, 0), (-1, 0), 9 if cols > 3 else 10),
        ('BOTTOMPADDING', (0, 0), (-1, 0), 10),
        ('TOPPADDING', (0, 0), (-1, 0), 10),
        ('ROWBACKGROUNDS', (0, 1), (-1, -1), [colors.white, colors.HexColor('#ecf0f1')]),
        ('GRID', (0, 0), (-1, -1), 0.5, colors.grey),
        ('VALIGN', (0, 0), (-1, -1), 'MIDDLE'),
        ('FONTNAME', (0, 1), (-1, -1), chinese_font_name or 'MicrosoftYaHei'),
        ('FONTSIZE', (0, 1), (-1, -1), 8 if cols > 3 else 9),
    ]
    
    return TableStyle(styles)


def generate_pdf_advanced(
    input_md=None,
    output_pdf="report.pdf",
    title="Untitled Report",
    author="AI Assistant",
    theme="business_blue",
    include_toc=True,
    markdown_content=None,
    data_tables=None,
    section_headers=None,
    custom_paragraphs=None
):
    """
    Advanced PDF generation with rich content support.
    
    Args:
        input_md: Path to Markdown file
        output_pdf: Output PDF path
        title: Document title
        author: Author name
        theme: Color theme
        include_toc: Include table of contents
        markdown_content: Raw markdown string (overrides input_md)
        data_tables: List of 2D arrays for tables
        section_headers: List of (header, content) tuples
        custom_paragraphs: Additional Paragraph objects
    
    Returns:
        str: Output PDF path
    """
    
    theme_color = THEMES.get(theme, THEMES["business_blue"])
    chinese_font = load_chinese_font()
    
    # Get content
    if markdown_content is None and input_md:
        try:
            with open(input_md, 'r', encoding='utf-8') as f:
                markdown_content = f.read()
        except FileNotFoundError:
            print(f"Warning: Input file not found: {input_md}")
    
    doc = SimpleDocTemplate(
        output_pdf,
        pagesize=A4,
        rightMargin=2*cm,
        leftMargin=2*cm,
        topMargin=2*cm,
        bottomMargin=2*cm
    )
    
    styles = getSampleStyleSheet()
    
    # Custom styles
    title_style = ParagraphStyle(
        'CustomTitle', parent=styles['Heading1'], fontSize=20,
        textColor=colors.HexColor(theme_color), spaceAfter=20,
        alignment=TA_CENTER, fontName=chinese_font or 'Helvetica-Bold'
    )
    
    heading2_style = ParagraphStyle(
        'CustomH2', parent=styles['Heading2'], fontSize=14,
        textColor=colors.HexColor('#34495e'), spaceBefore=15, spaceAfter=10,
        fontName=chinese_font or 'Helvetica-Bold'
    )
    
    normal_style = ParagraphStyle(
        'CustomNormal', parent=styles['Normal'], fontSize=10, leading=14,
        spaceBefore=5, spaceAfter=5, fontName=chinese_font or 'Helvetica'
    )
    
    h3_style = ParagraphStyle(
        'CustomH3', parent=styles['Heading3'], fontSize=12,
        textColor=colors.HexColor('#2c3e50'), spaceBefore=12, spaceAfter=8,
        fontName=chinese_font or 'Helvetica-Bold'
    )
    
    # Build story
    story = []
    
    # Title page
    story.append(Paragraph(title, title_style))
    story.append(Spacer(1, 0.5*cm))
    info_text = f"Author: {author}<br/>Date: {os.path.splitext(output_pdf)[0].split('_')[-1] if '_' in os.path.splitext(output_pdf)[0] else '2026'}"
    story.append(Paragraph(info_text, normal_style))
    story.append(Spacer(1, 1*cm))
    
    # Parse content if markdown provided
    if markdown_content:
        sections = parse_markdown_sections(markdown_content)
        
        # Table of Contents
        if include_toc:
            story.append(PageBreak())
            story.append(Paragraph("目录", title_style))
            story.append(Spacer(1, 0.3*cm))
            for i, sec in enumerate(sections[:6], 1):
                p = Paragraph(f"{i}. {sec['title'].replace('## ', '')}", normal_style)
                story.append(p)
            story.append(Spacer(1, 0.5*cm))
        
        # Process each section
        for i, section in enumerate(sections):
            title_text = section['title'].strip()
            content_text = section['content'].strip()
            
            if not title_text or not content_text:
                continue
            
            # Add section header
            if title_text.startswith('##'):
                story.append(Paragraph(title_text, heading2_style))
            else:
                story.append(Paragraph(title_text, h3_style))
            
            # Process content - look for tables
            content_lines = content_text.split('\n')
            table_rows = []
            in_table = False
            
            for line in content_lines:
                if '|' in line and '-' not in line.replace('||', ''):
                    # Check if it's a table row
                    cells = [cell.strip() for cell in line.split('|') if cell.strip()]
                    if len(cells) >= 2:
                        table_rows.append(cells)
                        in_table = True
                    else:
                        if table_rows:
                            # Render the table before adding this row
                            pass
                elif line.startswith('**') or line.startswith('- ') or line.startswith('• '):
                    if table_rows:
                        break
                    # Text content
                    para_text = line.lstrip('-•').lstrip('*').strip()
                    if para_text:
                        story.append(Paragraph(para_text, normal_style))
            
            if table_rows and len(table_rows) >= 2:
                t = Table(table_rows, colWidths=[None]*len(table_rows[0]))
                t.setStyle(build_table_style(table_rows, theme_color, chinese_font))
                story.append(t)
                story.append(Spacer(1, 0.5*cm))
                
                table_rows = []
            
            # Add remaining text content
            if in_table and table_rows:
                t = Table(table_rows, colWidths=[None]*len(table_rows[0]))
                t.setStyle(build_table_style(table_rows, theme_color, chinese_font))
                story.append(t)
                story.append(Spacer(1, 0.5*cm))
            
            story.append(Spacer(1, 0.5*cm))
    
    # Add any additional tables
    if data_tables:
        for table_data in data_tables:
            t = Table(table_data, colWidths=[None]*len(table_data[0]))
            t.setStyle(build_table_style(table_data, theme_color, chinese_font))
            story.append(t)
            story.append(Spacer(1, 0.5*cm))
    
    # Add any additional paragraphs
    if custom_paragraphs:
        for para in custom_paragraphs:
            story.append(para)
            story.append(Spacer(1, 0.3*cm))
    
    # Footer
    story.append(PageBreak())
    footer = Paragraph("=" * 60, normal_style)
    story.append(footer)
    story.append(Paragraph("Generated by OpenClaw PDF Generator Skill", normal_style))
    footer2 = Paragraph("=" * 60, normal_style)
    story.append(footer2)
    
    # Build PDF
    try:
        doc.build(story)
        print(f"[SUCCESS] PDF generated: {output_pdf}")
        return output_pdf
    except Exception as e:
        print(f"[ERROR] Failed to generate PDF: {e}")
        raise


def main():
    """CLI entry point."""
    import argparse
    
    parser = argparse.ArgumentParser(description='Generate professional PDF reports')
    parser.add_argument('--input', '-i', type=str, help='Input Markdown file')
    parser.add_argument('--output', '-o', type=str, default='report.pdf', help='Output PDF file')
    parser.add_argument('--title', '-t', type=str, default='Report', help='Document title')
    parser.add_argument('--author', '-a', type=str, default='OpenClaw AI', help='Author name')
    parser.add_argument('--theme', choices=['blue', 'green', 'orange', 'purple'], 
                       default='blue', help='Color theme')
    
    args = parser.parse_args()
    
    theme_map = {'blue': 'business_blue', 'green': 'nature_green', 
                 'orange': 'energy_orange', 'purple': 'corporate_purple'}
    
    generate_pdf_advanced(
        input_md=args.input,
        output_pdf=args.output,
        title=args.title,
        author=args.author,
        theme=theme_map[args.theme]
    )


if __name__ == '__main__':
    main()
