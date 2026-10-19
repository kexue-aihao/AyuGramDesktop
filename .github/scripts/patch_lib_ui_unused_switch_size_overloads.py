import re
import sys
from pathlib import Path


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_lib_ui_unused_switch_size_overloads.py <checkbox.cpp>")

    source_path = Path(sys.argv[1])
    source = source_path.read_bytes()
    for widget in (b"Check", b"Radio"):
        pattern = re.compile(
            rb"int SwitchDiameter\(not_null<const style::" + widget
            + rb" \*> st\) \{\r?\n"
            + rb"\treturn AyuUiSettings::isMaterialSwitches\(\) \? st->diameter : st::defaultToggleDiameter;\r?\n"
            + rb"\}\r?\n"
        )
        source, count = pattern.subn(b"", source)
        if count != 1:
            raise SystemExit(
                f"expected one unused SwitchDiameter({widget.decode()}) overload, found {count}"
            )

    source_path.write_bytes(source)


if __name__ == "__main__":
    main()
