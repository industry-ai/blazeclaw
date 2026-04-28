#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Test suite for PDF Generator Skill"""

import pytest
from pdf_generator import generate_pdf, load_chinese_font, THEMES


def test_load_chinese_font():
    """Test Chinese font loading"""
    font_name = load_chinese_font()
    assert font_name is not None  # Should find at least one font


def test_theme_colors():
    """Test predefined theme colors"""
    assert "business_blue" in THEMES
    assert THEMES["business_blue"] == "#3498db"
    assert len(THEMES) == 4  # All 4 themes present


def test_generate_sample_pdf(tmp_path):
    """Generate sample PDF for verification"""
    output_path = str(tmp_path / "test_report.pdf")
    
    data_tables = [
        ["指标", "数据", "时间"],
        ["测试值 1", "100", "2026-04-28"],
        ["测试值 2", "200", "2026-04-28"],
    ]
    
    generate_pdf(
        output_pdf=output_path,
        title="Test Report",
        author="OpenClaw Test",
        theme="business_blue",
        data_tables=[data_tables]
    )
    
    import os
    assert os.path.exists(output_path)
    assert os.path.getsize(output_path) > 1000  # Should be > 1KB


if __name__ == '__main__':
    pytest.main([__file__, '-v'])
