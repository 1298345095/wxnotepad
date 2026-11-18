"""Check actual native PDF body, page order, and selection/range semantics."""

import argparse
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET


def pdf_pages(tool, path):
    text = subprocess.check_output([tool, '-enc', 'UTF-8', '-layout', path, '-'], encoding='utf-8')
    return [page for page in text.split('\f') if page.strip()]


def markers(pages):
    return [int(item) for page in pages for item in re.findall(r'PrintLine(\d{4}) Unicode café', page)]


def check_wrapped(tool, path):
    pages = pdf_pages(tool, path)
    words = [int(item) for page in pages for item in re.findall(r'PrintWrap(\d{4})', page)]
    assert len(pages) >= 3
    assert words == list(range(1000, 2000)), 'wrapped page continuation lost or repeated text'
    return len(pages)


def print_labels(tool, path):
    data = subprocess.check_output([tool, '-i', '-xml', '-stdout', '-zoom', '1', path])
    tree = ET.fromstring(data)
    first_page = tree.find('page')
    nodes = first_page.findall('text')

    def label(node):
        font = tree.find(f'.//fontspec[@id="{node.attrib["font"]}"]')
        return tuple(int(node.attrib[key]) for key in ('left', 'top', 'width', 'height')) + (
            int(font.attrib['size']), font.attrib['color'].lower())

    def find(text):
        return next(node for node in nodes if text in ''.join(node.itertext()))

    return label(find('PrintWrap1000')), [label(node) for node in (nodes[0], nodes[1], find('Page 1'))]


def check_color_and_zoom(pdftotext, pdftohtml, color, monochrome):
    small_pages = check_wrapped(pdftotext, color)
    large_pages = check_wrapped(pdftotext, monochrome)
    small_label, color_decoration = print_labels(pdftohtml, color)
    large_label, monochrome_decoration = print_labels(pdftohtml, monochrome)
    small, foreground = small_label[-2:]
    large, black = large_label[-2:]
    assert color_decoration == monochrome_decoration, 'Print zoom changed the header, date, or footer'
    assert all(item[-2] < small and item[-1] == '#000000' for item in color_decoration), color_decoration
    assert foreground == '#dc4020' and black == '#000000', (foreground, black)
    assert small < large and small_pages < large_pages, (small, large, small_pages, large_pages)
    print(f'PASS: printed zoom increases body size from {small}pt to {large}pt; Black on white removes color')


def check(tool, whole, selection, page2, wrapped):
    total = pdf_pages(tool, whole)
    selected = pdf_pages(tool, selection)
    chosen = pdf_pages(tool, page2)
    assert len(total) >= 3 and len(selected) >= 3
    assert markers(total) == markers(selected) == list(range(1000, 1120))
    assert len(chosen) == 1 and markers(chosen) == markers([total[1]]) and markers(chosen)
    assert 'Page 2' in chosen[0]
    for pages in (total, selected):
        for index, page in enumerate(pages, 1):
            assert 'Untitled' in page and f'Page {index}' in page
    for marker in ('OutsideLeading', 'OutsideTrailing'):
        assert marker in ''.join(total) and marker in ''.join(selected)
    for marker in ('OutsideFirstLine', 'OutsideLastLine'):
        assert marker in ''.join(total) and marker not in ''.join(selected)
    if wrapped:
        check_wrapped(tool, wrapped)
    print(f'PASS: {len(total)} whole pages, {len(selected)} selection pages, and one matching range page')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--pdftotext', default='pdftotext')
    parser.add_argument('whole', type=Path)
    parser.add_argument('selection', type=Path)
    parser.add_argument('page2', type=Path)
    parser.add_argument('--wrapped', type=Path)
    parser.add_argument('--monochrome', type=Path, help='150% Black on white PDF for comparison with --wrapped')
    parser.add_argument('--pdftohtml', default='pdftohtml')
    args = parser.parse_args()
    if args.monochrome and not args.wrapped:
        parser.error('--monochrome requires --wrapped')
    check(args.pdftotext, args.whole, args.selection, args.page2, args.wrapped)
    if args.monochrome:
        check_color_and_zoom(args.pdftotext, args.pdftohtml, args.wrapped, args.monochrome)


if __name__ == '__main__':
    main()
