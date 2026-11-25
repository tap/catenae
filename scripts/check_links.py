#!/usr/bin/env python3
# Copyright 2026 Timothy Place and the catenae contributors
"""Every link inside the published site resolves (PLAN.md section 11.2, rule 7).

    scripts/check_links.py SITE_DIR [--base /catenae/]

Reads every HTML page of the composed site (the guide at the root, the reference under api/)
and checks each relative href and src: the file it names exists, a directory has an
index.html, and an #anchor into another page of the site is an id or a name there. A
site-absolute link is read against --base, the path the site is served from (mdBook's 404 page
links that way). Links to other sites (http, https, mailto) are not fetched.
"""
import argparse
import html.parser
import pathlib
import sys
import urllib.parse


class Links(html.parser.HTMLParser):
    def __init__(self):
        super().__init__()
        self.links, self.anchors = [], set()

    def handle_starttag(self, tag, attrs):
        for name, value in attrs:
            if value is None:
                continue
            if name in ("href", "src"):
                self.links.append(value)
            if name in ("id", "name"):
                self.anchors.add(value)


def parse(page: pathlib.Path) -> Links:
    parser = Links()
    parser.feed(page.read_text(errors="replace"))
    return parser


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("site")
    ap.add_argument("--base", default="/catenae/", help="the path the site is served from")
    args = ap.parse_args()
    site = pathlib.Path(args.site).resolve()
    pages = sorted(site.rglob("*.html"))
    if not pages:
        raise SystemExit(f"{site}: no pages")
    parsed = {page: parse(page) for page in pages}
    broken, checked = [], 0
    for page, links in parsed.items():
        for link in links.links:
            url = urllib.parse.urlparse(link)
            if url.scheme or link.startswith(("//", "#")) or not url.path:
                continue
            checked += 1
            path = urllib.parse.unquote(url.path)
            if path.startswith("/"):
                if not path.startswith(args.base):
                    broken.append(f"{page.relative_to(site)}: {link} (outside the site's base {args.base})")
                    continue
                target = (site / path[len(args.base):]).resolve()
            else:
                target = (page.parent / path).resolve()
            if target.is_dir():
                target = target / "index.html"
            if not target.is_file() or site not in target.parents and target != site:
                broken.append(f"{page.relative_to(site)}: {link} (no such file in the site)")
                continue
            if url.fragment and target.suffix == ".html":
                anchors = parsed[target].anchors if target in parsed else parse(target).anchors
                if url.fragment not in anchors:
                    broken.append(f"{page.relative_to(site)}: {link} (no anchor #{url.fragment})")
    for b in broken:
        print(b)
    print(f"{len(pages)} pages, {checked} links checked, {len(broken)} broken")
    return 1 if broken else 0


if __name__ == "__main__":
    sys.exit(main())
