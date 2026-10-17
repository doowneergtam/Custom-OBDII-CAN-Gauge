"""Package the phone setup page for the firmware.

    python tools/build_web.py

Takes design/web-config/index.html, wraps it in a document skeleton, drops the
Google Fonts link (the gauge has no internet; the fallback stacks are used),
gzips it, and writes firmware/components/webcfg/page.html.gz, which the
webcfg component embeds. Run this whenever the page changes, then rebuild.
"""
import gzip, pathlib, re

root = pathlib.Path(__file__).resolve().parent.parent
src = root / "design" / "web-config" / "index.html"
dst = root / "firmware" / "components" / "webcfg" / "page.html.gz"

html = src.read_text(encoding="utf-8")
html = re.sub(r'<link rel="preconnect"[^>]*>\s*', "", html)
html = re.sub(r'<link rel="stylesheet" href="https://fonts\.googleapis\.com[^>]*>\s*', "", html)
page = ('<!doctype html><html><head><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">'
        '<style>body{margin:0;font:14px system-ui}[hidden]{display:none!important}</style></head><body>'
        + html + "</body></html>")
data = gzip.compress(page.encode("utf-8"), 9)
dst.parent.mkdir(parents=True, exist_ok=True)
dst.write_bytes(data)
print(f"{dst.relative_to(root)}: {len(page)} -> {len(data)} bytes")
