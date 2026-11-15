#!/usr/bin/env python3
"""Check local Markdown links and examples copied from compiled source snippets."""

import argparse
from html.parser import HTMLParser
from pathlib import Path
import re
import sys
import textwrap
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
SNIPPET = re.compile(
    r"<!-- snippet: ([^:]+):([^ ]+) -->\n```cpp\n(.*?)\n```\n<!-- end snippet -->",
    re.DOTALL,
)


def snippet_text(source: str, name: str) -> str:
    """Read exactly one pair of named // [snippet] markers."""
    marker = f"// [{name}]"
    parts = (ROOT / source).read_text().split(marker)
    if len(parts) != 3:
        raise ValueError(f"{source}: expected two {marker} markers")
    return textwrap.dedent(parts[1]).strip()


def check() -> list[str]:
    """Return broken local links and snippets that have drifted from their source."""
    errors = []
    pages = [ROOT / "README.md", *sorted((ROOT / "docs").glob("*.md"))]
    for page in pages:
        text = page.read_text()
        for source, name, copied in SNIPPET.findall(text):
            try:
                if copied != snippet_text(source, name):
                    errors.append(f"{page.relative_to(ROOT)}: stale snippet {source}:{name}")
            except (OSError, ValueError) as error:
                errors.append(str(error))
        if text.count("<!-- snippet:") != len(SNIPPET.findall(text)):
            errors.append(f"{page.relative_to(ROOT)}: malformed snippet block")
        # Code examples may contain bracket/parenthesis syntax that is not a link.
        prose = re.sub(r"```.*?```", "", text, flags=re.DOTALL)
        for link in re.findall(r"\[[^\]]+\]\(([^)]+)\)", prose):
            path = link.split("#", 1)[0]
            if not path or re.match(r"[a-z]+:", path):
                continue
            if not (page.parent / path).exists():
                errors.append(f"{page.relative_to(ROOT)}: broken link {link}")
    return errors


class HtmlLinks(HTMLParser):
    """Collect generated navigation targets and anchors without browser dependencies."""

    def __init__(self, source: str):
        super().__init__()
        self.links = []
        self.anchors = set()
        self.feed(source)

    def handle_starttag(self, tag, attributes):
        attributes = dict(attributes)
        for key in ("href", "src"):
            if key in attributes:
                self.links.append(attributes[key])
        if "id" in attributes:
            self.anchors.add(attributes["id"])
        if tag == "a" and "name" in attributes:
            self.anchors.add(attributes["name"])


def check_html(directory: Path) -> list[str]:
    """Check generated files, symbol/guide anchors, and representative APIs."""
    directory = directory.resolve()
    pages = {path.resolve(): HtmlLinks(path.read_text()) for path in directory.rglob("*.html")}
    errors = []
    required = [
        "index.html",
        "classcafe_1_1ClassFile.html",
        "classcafe_1_1MethodCode.html",
        "classcafe_1_1archive_1_1Archive.html",
        "classcafe_1_1analysis_1_1FrameAnalysis.html",
        "classcafe_1_1detail_1_1ClassReader.html",
        *[f"group__{name}.html" for name in
          ("model", "bytecode", "io", "archive", "classpath", "analysis", "implementation")],
    ]
    for name in required:
        if directory / name not in pages:
            errors.append(f"Missing generated reference page: {name}")
    for page, content in pages.items():
        for link in content.links:
            parts = urlsplit(link)
            if parts.scheme or parts.netloc:
                continue
            target = (page.parent / unquote(parts.path)).resolve() if parts.path else page
            if not target.exists():
                errors.append(f"{page.name}: broken generated link {link}")
            elif parts.fragment and target in pages:
                fragment = unquote(parts.fragment)
                # Doxygen 1.18 emits index_~ links for index__7E and omits some
                # small enum/type index headings. Exempt only its alphabetical
                # member-index shortcuts; all symbol and guide anchors are checked.
                if target.name.startswith("functions") and fragment.startswith("index_"):
                    continue
                if fragment not in pages[target].anchors:
                    errors.append(f"{page.name}: missing generated anchor {link}")
    return sorted(set(errors))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--html", type=Path, help="also validate generated Doxygen HTML")
    args = parser.parse_args()
    issues = check()
    if args.html:
        issues += check_html(args.html)
    print("\n".join(issues) if issues else "Documentation links and compiled snippets are synchronized.")
    sys.exit(bool(issues))
