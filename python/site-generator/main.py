import sys
import os

import builder

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
CONTENT_DIR = os.path.join(BASE_DIR, "content")
TEMPLATES_DIR = os.path.join(BASE_DIR, "templates")
OUTPUT_DIR = os.path.join(BASE_DIR, "build")


def main():
    if len(sys.argv) < 2 or sys.argv[1] != "build":
        print("usage: python3 main.py build")
        sys.exit(1)

    rebuilt, skipped = builder.build(CONTENT_DIR, TEMPLATES_DIR, OUTPUT_DIR)

    for f in rebuilt:
        print(f"rebuilt: {f}")
    for f in skipped:
        print(f"skipped (unchanged): {f}")

    print(f"\n{len(rebuilt)} rebuilt, {len(skipped)} skipped, output -> {OUTPUT_DIR}")


if __name__ == "__main__":
    main()
