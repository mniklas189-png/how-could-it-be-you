"""Writes the English texts of Tools/Setup/english.py into Game/Content/Localization/Game/en/Game.po.

Run Tools/Localize.bat first (it writes Game.po with every text of the code), then this, then Localize.bat again
(it takes Game.po in and compiles Game.locres). Prints the keys that have no English text yet.
Usage: python -I Tools/Setup/FillEnglish.py
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from english import EN  # noqa: E402

PO = HERE.parent.parent / 'Game' / 'Content' / 'Localization' / 'Game' / 'en' / 'Game.po'


def po_escape(text: str) -> str:
    return text.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n').replace('\t', '\\t')


def main() -> int:
    if not PO.exists():
        print(f'{PO} is missing: run Tools/Localize.bat first')
        return 1
    lines = PO.read_text(encoding='utf-8-sig').splitlines()
    out = []
    key = None
    filled = 0
    missing = []
    for line in lines:
        m = re.match(r'msgctxt "HC,(.+)"$', line)
        if m:
            key = m.group(1)
        if line.startswith('msgstr ') and key is not None:
            if key in EN:
                line = f'msgstr "{po_escape(EN[key])}"'
                filled += 1
            elif line == 'msgstr ""':
                missing.append(key)
            key = None
        out.append(line)
    PO.write_text('\n'.join(out) + '\n', encoding='utf-8-sig')
    print(f'{filled} English texts written into {PO}')
    if missing:
        print('No English text yet for:', ', '.join(missing))
    return 0


if __name__ == '__main__':
    sys.exit(main())
