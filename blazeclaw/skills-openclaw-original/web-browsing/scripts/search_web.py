#!/usr/bin/env python3
"""
Web search utility for the web-browsing skill.
Searches the web and returns relevant results.
"""

import json
import re
import urllib.parse
import urllib.request

try:
    import requests  # type: ignore
except Exception:
    requests = None

try:
    from bs4 import BeautifulSoup  # type: ignore
except Exception:
    BeautifulSoup = None


def _http_get_text(url: str, timeout: int = 10) -> str:
    if requests is not None:
        headers = {
            'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'
        }
        response = requests.get(url, headers=headers, timeout=timeout)
        response.raise_for_status()
        return response.text

    req = urllib.request.Request(
        url,
        headers={
            'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'
        })
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        charset = resp.headers.get_content_charset() or 'utf-8'
        return resp.read().decode(charset, errors='replace')


def _strip_html(text: str) -> str:
    cleaned = re.sub(r'<script[\s\S]*?</script>', ' ', text, flags=re.IGNORECASE)
    cleaned = re.sub(r'<style[\s\S]*?</style>', ' ', cleaned, flags=re.IGNORECASE)
    cleaned = re.sub(r'<[^>]+>', ' ', cleaned)
    cleaned = re.sub(r'\s+', ' ', cleaned)
    return cleaned.strip()


def search_web(query: str, num_results: int = 5) -> list[dict]:
    """
    Perform a web search and return results.
    
    Args:
        query: Search query string
        num_results: Number of results to return (default: 5)
    
    Returns:
        List of dictionaries with title, url, and snippet
    """
    # Using DuckDuckGo HTML API (no API key required)
    search_url = f"https://html.duckduckgo.com/html/?q={urllib.parse.quote(query)}"
    
    try:
        html = _http_get_text(search_url, timeout=10)
        results = []

        if BeautifulSoup is not None:
            soup = BeautifulSoup(html, 'html.parser')
            for result in soup.find_all('a', class_='result__a')[:num_results]:
                title = result.get_text()
                url = result.get('href', '')

                snippet_div = result.find_parent().find_next_sibling('div', class_='result__snippet')
                snippet = snippet_div.get_text() if snippet_div else ''

                results.append({
                    'title': title,
                    'url': url,
                    'snippet': snippet
                })
        else:
            anchor_pattern = re.compile(
                r'<a[^>]*class="result__a"[^>]*href="([^"]+)"[^>]*>(.*?)</a>',
                flags=re.IGNORECASE | re.DOTALL)
            snippet_pattern = re.compile(
                r'<a[^>]*class="result__a"[^>]*>.*?</a>[\s\S]{0,1200}?<a[^>]*class="result__snippet"[^>]*>(.*?)</a>',
                flags=re.IGNORECASE | re.DOTALL)

            anchors = list(anchor_pattern.finditer(html))
            for i, match in enumerate(anchors[:num_results]):
                url = match.group(1)
                title = _strip_html(match.group(2))
                snippet = ''
                snippet_match = snippet_pattern.search(html, pos=match.start())
                if snippet_match:
                    snippet = _strip_html(snippet_match.group(1))

                results.append({
                    'title': title,
                    'url': url,
                    'snippet': snippet
                })
        
        return results
    
    except Exception as e:
        return [{'error': f'Search failed: {str(e)}'}]


def fetch_url(url: str) -> dict:
    """
    Fetch and parse a webpage.
    
    Args:
        url: URL to fetch
    
    Returns:
        Dictionary with title, content, and metadata
    """
    try:
        html = _http_get_text(url, timeout=15)

        if BeautifulSoup is None:
            title_match = re.search(r'<title[^>]*>(.*?)</title>', html, flags=re.IGNORECASE | re.DOTALL)
            title = _strip_html(title_match.group(1)) if title_match else 'No title found'
            content = _strip_html(html)
            return {
                'url': url,
                'title': title,
                'content': content[:5000],
                'status': 'success'
            }

        soup = BeautifulSoup(html, 'html.parser')
        
        # Extract title
        title_tag = soup.find('title')
        title = title_tag.get_text().strip() if title_tag else 'No title found'
        
        # Remove script and style elements
        for element in soup(['script', 'style', 'nav', 'footer']):
            element.decompose()
        
        # Get main content (prefer article, otherwise body)
        article = soup.find('article') or soup.find('main') or soup.find('body')
        content = article.get_text(separator='\n\n', strip=True) if article else ''
        
        return {
            'url': url,
            'title': title,
            'content': content[:5000],  # Limit to first 5000 chars
            'status': 'success'
        }
    
    except Exception as e:
        return {'error': f'Failed to fetch URL: {str(e)}', 'url': url}


if __name__ == '__main__':
    import sys
    
    if len(sys.argv) < 2:
        print("Usage: search_web.py <search_query|url> [--fetch]")
        sys.exit(1)
    
    query = sys.argv[1]
    fetch_mode = '--fetch' in sys.argv
    
    if fetch_mode:
        result = fetch_url(query)
    else:
        result = search_web(query)
    
    print(json.dumps(result, ensure_ascii=False, indent=2))
