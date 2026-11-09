import gzip
from pathlib import Path
import re
import sys


def minify_css(css: str) -> str:
    parts = []
    index = 0
    quote = None
    while index < len(css):
        character = css[index]
        if quote is not None:
            parts.append(character)
            if character == "\\" and index + 1 < len(css):
                index += 1
                parts.append(css[index])
            elif character == quote:
                quote = None
            index += 1
            continue

        if character in ("'", '"'):
            quote = character
            parts.append(character)
            index += 1
            continue

        if css.startswith("/*", index):
            comment_end = css.find("*/", index + 2)
            index = len(css) if comment_end < 0 else comment_end + 2
            continue

        if character.isspace():
            while index < len(css) and css[index].isspace():
                index += 1
            parts.append(" ")
            continue

        parts.append(character)
        index += 1

    compact = "".join(parts)
    compact = re.sub(r"\s*([{};,])\s*", r"\1", compact)
    return compact.strip()


def compact_web_document(source: bytes) -> bytes:
    text = source.decode("utf-8")
    normalized = text.replace("\r\n", "\n").replace("\r", "\n")
    normalized = re.sub(
        r"(?s)(<style>)(.*?)(</style>)",
        lambda match: (
            f"{match.group(1)}{minify_css(match.group(2))}{match.group(3)}"
        ),
        normalized,
    )
    # Keep line breaks for JavaScript ASI, but drop indentation bytes.
    return "\n".join(line.lstrip(" \t") for line in normalized.split("\n")).encode(
        "utf-8"
    )


def main() -> int:
    if len(sys.argv) != 4:
        print(
            "usage: gzip_file.py SOURCE DESTINATION MAX_BYTES",
            file=sys.stderr,
        )
        return 2

    source = Path(sys.argv[1])
    destination = Path(sys.argv[2])
    max_bytes = int(sys.argv[3])
    if max_bytes <= 0:
        print("MAX_BYTES must be positive", file=sys.stderr)
        return 2

    destination.parent.mkdir(parents=True, exist_ok=True)
    source_bytes = source.read_bytes()
    compact_bytes = compact_web_document(source_bytes)

    with destination.open("wb") as output:
        with gzip.GzipFile(
            filename="index.html",
            mode="wb",
            compresslevel=9,
            fileobj=output,
            mtime=0,
        ) as archive:
            archive.write(compact_bytes)

    compressed_size = destination.stat().st_size
    if compressed_size > max_bytes:
        destination.unlink(missing_ok=True)
        print(
            f"compressed web UI is {compressed_size} bytes; "
            f"budget is {max_bytes} bytes",
            file=sys.stderr,
        )
        return 1

    print(
        f"compressed web UI: {compressed_size} / {max_bytes} bytes "
        f"({compressed_size * 100 / max_bytes:.1f}%), "
        f"source {len(source_bytes)} -> {len(compact_bytes)} bytes"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
