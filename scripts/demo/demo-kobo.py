#!/usr/bin/env python3
"""Creates a fake Kobo Clara 2E with a few books, for screenshots and manual tests.

Usage: demo-kobo.py <folder> <demo Komga URL>

The folder gets what Kobold reads on a real Kobo: .kobo/version (a made-up serial),
.kobo/KoboReader.sqlite with some books, their files and their library thumbnails (drawn by
the demo Komga server). Point KOBOLD_MEDIA_ROOT at the folder's parent.
"""

import io
import json
import os
import sqlite3
import sys
import urllib.request

from PIL import Image, ImageDraw, ImageFont

SERIAL = "N00000A000386"
USER = "11111111-1111-4111-8111-111111111111"

# (Komga book id or None, title, author, series, number, read status, percent read, path or
# store id)
BOOKS = [
    ("SVERNE-1", "Five Weeks in a Balloon", "Jules Verne", "Voyages extraordinaires", 1, 2, 100, None),
    ("SVERNE-2", "Journey to the Centre of the Earth", "Jules Verne", "Voyages extraordinaires", 2, 1, 64, None),
    ("SHOLMES-1", "A Study in Scarlet", "Arthur Conan Doyle", "Sherlock Holmes", 1, 2, 100, None),
    ("SFRANK-1", "Frankenstein", "Mary Shelley", "", 0, 0, 0, None),
    ("SNEMO-1", "Little Nemo in Slumberland 1905", "Winsor McCay", "Little Nemo in Slumberland", 1, 1, 22, None),
    (None, "The Odyssey", "Homer", "", 0, 1, 37, "22222222-2222-4222-8222-222222222222"),
]


def qt3_hash(text):
    value = 0
    for byte in text.encode():
        value = ((value << 4) + byte) & 0xFFFFFFFF
        value ^= (value & 0xF0000000) >> 23
        value &= 0x0FFFFFFF
    return value


def thumbnail(root, image_id, data):
    value = qt3_hash(image_id)
    folder = os.path.join(root, ".kobo-images", str(value & 0xFF), str((value & 0xFF00) >> 8))
    os.makedirs(folder, exist_ok=True)
    image = Image.open(io.BytesIO(data)).convert("RGB")
    image.thumbnail((240, 360))
    image.save(os.path.join(folder, f"{image_id} - N3_LIBRARY_GRID.parsed"), "JPEG", quality=88)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip().splitlines()[2])
    root, komga = sys.argv[1], sys.argv[2].rstrip("/")
    os.makedirs(os.path.join(root, ".kobo", "Kobo"), exist_ok=True)
    with open(os.path.join(root, ".kobo", "version"), "w") as file:
        file.write(f"{SERIAL},4.1.15,4.38.23828,4.1.15,4.1.15,00000000-0000-0000-0000-000000000386\n")
    with open(os.path.join(root, ".kobo", "Kobo", "Kobo eReader.conf"), "w") as file:
        file.write("[ApplicationPreferences]\nSideloadedMode=false\n")

    database = sqlite3.connect(os.path.join(root, ".kobo", "KoboReader.sqlite"))
    database.executescript("""
        CREATE TABLE user (UserID TEXT NOT NULL, UserDisplayName TEXT);
        CREATE TABLE content (ContentID TEXT NOT NULL, ContentType TEXT NOT NULL, MimeType TEXT,
            BookID TEXT, Title TEXT, Attribution TEXT, Series TEXT, SeriesNumber TEXT, SeriesID TEXT,
            SeriesNumberFloat REAL, IsDownloaded BOOL DEFAULT 'true', ReadStatus INTEGER,
            ___PercentRead INTEGER, DateLastRead TEXT, ___ExpirationStatus INT, ___UserID TEXT NOT NULL,
            ImageId TEXT, PRIMARY KEY (ContentID));
    """)
    database.execute("INSERT INTO user VALUES (?, 'reader')", (USER,))
    manifest = []
    for day, (book_id, title, author, series, number, status, percent, store) in enumerate(BOOKS, start=1):
        if store:
            content_id, user, image_id = store, USER, store
        else:
            extension = ".kepub.epub"
            path = f"kobold/{author} - {title}{extension}"
            os.makedirs(os.path.join(root, "kobold"), exist_ok=True)
            open(os.path.join(root, path), "wb").close()
            content_id, user = "file:///mnt/onboard/" + path, ""
            # Kobold's manifest, as after a sync from the demo Komga server: same hash, date and
            # size as the server's book, so the next sync keeps the file.
            comic = book_id.startswith("SNEMO")
            volume = int(book_id.rsplit("-", 1)[1])
            manifest.append({
                "komgaBookId": book_id, "path": path,
                "sourceMediaType": "application/zip" if comic else "application/epub+zip",
                "komgaFileHash": f"demo{book_id.lower()}", "komgaFileLastModified": "2026-01-01T00:00:00Z",
                "komgaSizeBytes": 1_200_000 + volume * 37_000, "outputSizeBytes": 1_100_000,
                "syncedAt": f"2026-10-{day:02d}T08:00:00Z",
                **({"conversionProfile": "KoC", "readingDirection": "ltr"} if comic else {}),
                **({"series": series, "seriesNumber": number} if series else {}),
            })
            image_id = content_id.replace("file:///", "file____").replace("/", "_").replace(" ", "_").replace(".", "_")
        last_read = f"2026-10-{day:02d}T20:15:00Z" if status else None
        database.execute(
            "INSERT INTO content VALUES (?, '6', 'application/x-kobo-epub+zip', NULL, ?, ?, ?, ?, ?, ?, 'true', ?, ?, ?, 0, ?, ?)",
            (content_id, title, author, series or None, str(number) if series else None, series or None,
             float(number) if series else None, status, percent, last_read, user, image_id))
        url = f"{komga}/api/v1/books/{book_id}/thumbnail" if book_id else None
        if url:
            thumbnail(root, image_id, urllib.request.urlopen(url).read())
        else:
            # A store book: a plain cover with its title.
            image = Image.new("RGB", (240, 360), "#2f3e46")
            draw = ImageDraw.Draw(image)
            draw.rectangle([10, 10, 229, 349], outline="#d9c48a", width=2)
            draw.text((120, 160), title, font=ImageFont.load_default(26), fill="#f4efe1", anchor="mm")
            draw.text((120, 300), author, font=ImageFont.load_default(18), fill="#d9c48a", anchor="mm")
            buffer = io.BytesIO()
            image.save(buffer, "PNG")
            thumbnail(root, image_id, buffer.getvalue())
    database.commit()
    database.close()
    os.makedirs(os.path.join(root, ".kobold"), exist_ok=True)
    with open(os.path.join(root, ".kobold", "manifest.json"), "w") as file:
        json.dump({"version": 1, "komgaUrl": komga, "books": manifest}, file, indent=2)
    print(f"demo Kobo {SERIAL} in {root}")


if __name__ == "__main__":
    main()
