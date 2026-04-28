#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Pre-built PDF report templates for common use cases."""

from pdf_generator import generate_pdf


def business_report_template(
    title="Business Report",
    data_summary=None,
    table_data=None,
    output_file="business_report.pdf"
):
    """
    Generate a professional business report PDF.
    
    Args:
        title: Report title
        data_summary: Dictionary of key metrics
        table_data: 2D array for main data table
        output_file: Output PDF filename
    
    Returns:
        str: Path to generated PDF
    """
    if data_summary is None:
        data_summary = {}
    
    # Build custom content from summary
    content_items = []
    for key, value in data_summary.items():
        content_items.append(f"<b>{key}:</b> {value}")
    
    return generate_pdf(
        output_pdf=output_file,
        title=title,
        author="Business Intelligence Team",
        theme="business_blue",
        custom_content=content_items,
        data_tables=[table_data] if table_data else None
    )


def research_paper_template(
    title="Research Paper",
    abstract=None,
    sections=None,
    tables=None,
    output_file="research_paper.pdf"
):
    """
    Generate an academic-style research paper PDF.
    
    Args:
        title: Paper title
        abstract: Abstract text
        sections: List of (header, content) tuples
        tables: List of data tables
        output_file: Output PDF filename
    
    Returns:
        str: Path to generated PDF
    """
    story = [abstract] if abstract else []
    
    if sections:
        for header, content in sections:
            story.append(header)
            story.append(content)
    
    return generate_pdf(
        output_pdf=output_file,
        title=title,
        author="Research Team",
        theme="nature_green",
        custom_content=story,
        data_tables=tables
    )


def executive_summary_template(
    title="Executive Summary",
    key_findings=None,
    recommendations=None,
    output_file="executive_summary.pdf"
):
    """
    Generate a concise executive summary PDF.
    
    Args:
        title: Document title
        key_findings: List of key findings
        recommendations: List of recommendations
        output_file: Output PDF filename
    
    Returns:
        str: Path to generated PDF
    """
    if key_findings is None:
        key_findings = []
    if recommendations is None:
        recommendations = []
    
    content = ["<b>Key Findings:</b>", ""] + \
              [f"• {finding}" for finding in key_findings] + \
              ["", "<b>Recommendations:</b>", ""] + \
              [f"• {rec}" for rec in recommendations]
    
    return generate_pdf(
        output_pdf=output_file,
        title=title,
        author="Executive Office",
        theme="corporate_purple",
        custom_content=content
    )
