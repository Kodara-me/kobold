#!/usr/bin/env python3
"""A fake Komga server with public-domain books, for screenshots and manual tests.

Usage: demo-komga.py <port> <cover cache folder>

It answers the part of Komga's API that Kobold uses (libraries, series, books, collections,
read lists, thumbnails, the current user) from the catalogue below; every API key is
accepted. Covers are drawn with Pillow and cached in the given folder. It never stores
anything else and only listens on 127.0.0.1.
"""

import io
import json
import os
import subprocess
import sys
import textwrap
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

from PIL import Image, ImageDraw, ImageFont

# (series id, library id, title, author, genres, cover colour, [book titles]); a series with
# one book is a stand-alone novel, as Komga shows them.
CATALOGUE = [
    ("SVERNE", "LNOVELS", "Voyages extraordinaires", "Jules Verne", ["Adventure", "Science fiction"], "#1f4e5f",
     ["Five Weeks in a Balloon", "Journey to the Centre of the Earth", "From the Earth to the Moon",
      "Twenty Thousand Leagues Under the Seas", "Around the World in Eighty Days", "The Mysterious Island"]),
    ("SHOLMES", "LNOVELS", "Sherlock Holmes", "Arthur Conan Doyle", ["Mystery"], "#5a2e1f",
     ["A Study in Scarlet", "The Sign of the Four", "The Adventures of Sherlock Holmes",
      "The Hound of the Baskervilles", "The Valley of Fear"]),
    ("SALICE", "LNOVELS", "Alice", "Lewis Carroll", ["Fantasy"], "#6b3f7a",
     ["Alice's Adventures in Wonderland", "Through the Looking-Glass"]),
    ("SMUSKET", "LNOVELS", "The Three Musketeers", "Alexandre Dumas", ["Adventure"], "#7a2531",
     ["The Three Musketeers"]),
    ("STREASURE", "LNOVELS", "Treasure Island", "Robert Louis Stevenson", ["Adventure"], "#2f5d3a",
     ["Treasure Island"]),
    ("SFRANK", "LNOVELS", "Frankenstein", "Mary Shelley", ["Gothic", "Science fiction"], "#2b2d42",
     ["Frankenstein"]),
    ("SDRACULA", "LNOVELS", "Dracula", "Bram Stoker", ["Gothic"], "#4a0e0e", ["Dracula"]),
    ("SMOBY", "LNOVELS", "Moby-Dick", "Herman Melville", ["Adventure"], "#24496b", ["Moby-Dick"]),
    ("STIME", "LNOVELS", "The Time Machine", "H. G. Wells", ["Science fiction"], "#3d5a80",
     ["The Time Machine"]),
    ("SPRIDE", "LNOVELS", "Pride and Prejudice", "Jane Austen", ["Romance"], "#8a5a44",
     ["Pride and Prejudice"]),
    ("SNEMO", "LCOMICS", "Little Nemo in Slumberland", "Winsor McCay", ["Comics", "Fantasy"], "#8f5a12",
     ["1905", "1906", "1907", "1908"]),
    ("SKAT", "LCOMICS", "Krazy Kat", "George Herriman", ["Comics", "Humour"], "#b5442f",
     ["1916", "1917"]),
]
LIBRARIES = [{"id": "LNOVELS", "name": "Novels"}, {"id": "LCOMICS", "name": "Comics"}]
COLLECTIONS = [{"id": "CADVENTURE", "name": "Great adventures", "ordered": False,
                "seriesIds": ["SVERNE", "SMUSKET", "STREASURE", "SMOBY"]}]
READLISTS = [{"id": "RMYSTERY", "name": "Rainy-day mysteries", "ordered": True,
              "bookIds": ["SHOLMES-1", "SHOLMES-2", "SHOLMES-4"]}]


def build():
    series, books = {}, {}
    for sid, library, title, author, genres, colour, titles in CATALOGUE:
        comic = library == "LCOMICS"
        series[sid] = {
            "id": sid, "libraryId": library, "name": title, "booksCount": len(titles),
            "metadata": {"title": title, "genres": genres, "ageRating": None,
                         "readingDirection": "LEFT_TO_RIGHT" if comic else ""},
            "_author": author, "_colour": colour,
        }
        for number, book_title in enumerate(titles, start=1):
            bid = f"{sid}-{number}"
            full_title = f"{title} {book_title}" if comic else book_title
            books[bid] = {
                "id": bid, "seriesId": sid, "seriesTitle": title, "libraryId": library,
                "name": full_title, "url": f"/books/{bid}", "fileHash": f"demo{bid.lower()}",
                "fileLastModified": "2026-01-01T00:00:00Z", "sizeBytes": 1_200_000 + number * 37_000,
                "media": {"mediaType": "application/zip" if comic else "application/epub+zip",
                          "status": "READY", "pagesCount": 48 if comic else 320, "epubIsKepub": False},
                "metadata": {"title": full_title, "number": str(number), "numberSort": number,
                             "authors": [{"name": author, "role": "writer"}]},
                "readProgress": None,
                "_author": author, "_colour": colour, "_number": number, "_count": len(titles),
            }
    return series, books


SERIES, BOOKS = build()


def public(item):
    return {key: value for key, value in item.items() if not key.startswith("_")}


def font(size, bold=False):
    try:
        path = subprocess.run(["fc-match", "-f", "%{file}", "Noto Serif:bold" if bold else "Noto Serif"],
                              capture_output=True, text=True, check=True).stdout
        return ImageFont.truetype(path, size)
    except Exception:
        return ImageFont.load_default(size)


def cover(book, cache):
    path = os.path.join(cache, book["id"] + ".png")
    if os.path.exists(path):
        return open(path, "rb").read()
    width, height = 600, 900
    base = Image.new("RGB", (width, height), book["_colour"])
    draw = ImageDraw.Draw(base)
    # A soft vertical light, a frame and a rule: a classic cloth binding.
    r, g, b = base.getpixel((0, 0))
    for y in range(height):
        k = 1.18 - 0.36 * y / height
        draw.line([(0, y), (width, y)], fill=(min(255, int(r * k)), min(255, int(g * k)), min(255, int(b * k))))
    gold = (226, 196, 128)
    draw.rectangle([28, 28, width - 29, height - 29], outline=gold, width=4)
    draw.rectangle([44, 44, width - 45, height - 45], outline=gold, width=1)
    title = book["metadata"]["title"]
    if book["_count"] > 1:
        draw.ellipse([width / 2 - 32, 78, width / 2 + 32, 142], outline=gold, width=3)
        draw.text((width / 2, 110), str(book["_number"]), font=font(32, bold=True), fill=gold, anchor="mm")
        if book["seriesTitle"] not in title:
            draw.text((width / 2, 190), book["seriesTitle"].upper(), font=font(24), fill=gold, anchor="mm")
    # The largest title size whose lines fit inside the frame.
    for size, wrap in ((60, 14), (52, 16), (46, 18), (40, 20)):
        lines = textwrap.wrap(title, width=wrap)
        big = font(size, bold=True)
        if len(lines) <= 4 and max(draw.textlength(line, font=big) for line in lines) <= width - 130:
            break
    step = size * 1.25
    y = height / 2 + 10 - (len(lines) - 1) * step / 2
    for line in lines:
        draw.text((width / 2, y), line, font=big, fill=(250, 244, 230), anchor="mm")
        y += step
    draw.line([(width / 2 - 90, y), (width / 2 + 90, y)], fill=gold, width=2)
    draw.text((width / 2, height - 120), book["_author"], font=font(32), fill=gold, anchor="mm")
    buffer = io.BytesIO()
    base.save(buffer, "PNG", optimize=True)
    data = buffer.getvalue()
    with open(path, "wb") as file:
        file.write(data)
    return data


def condition_value(body, field):
    condition = body.get("condition") or {}
    if field in condition:
        return condition[field].get("value")
    for part in condition.get("allOf", []) + condition.get("anyOf", []):
        if field in part:
            return part[field].get("value")
    return None


def page(items, query):
    size = int(query.get("size", ["500"])[0])
    number = int(query.get("page", ["0"])[0])
    chunk = items[number * size:(number + 1) * size]
    return {"content": chunk, "number": number, "size": size, "totalElements": len(items),
            "totalPages": max(1, -(-len(items) // size)), "last": (number + 1) * size >= len(items),
            "first": number == 0, "empty": not chunk}


class Handler(BaseHTTPRequestHandler):
    cache = "."

    def log_message(self, format, *args):
        pass

    def send(self, status, payload, content_type="application/json"):
        data = payload if isinstance(payload, bytes) else json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def body(self):
        length = int(self.headers.get("Content-Length") or 0)
        try:
            return json.loads(self.rfile.read(length) or b"{}")
        except json.JSONDecodeError:
            return {}

    def do_GET(self):
        url = urlparse(self.path)
        query = parse_qs(url.query)
        parts = [part for part in url.path.split("/") if part]
        if url.path == "/api/v2/users/me":
            return self.send(200, {"id": "DEMOUSER", "email": "reader@example.org", "roles": ["ADMIN", "FILE_DOWNLOAD"]})
        if url.path == "/api/v2/users/me/api-keys":
            return self.send(200, [])
        if url.path == "/api/v1/libraries":
            return self.send(200, LIBRARIES)
        if url.path == "/api/v1/genres":
            return self.send(200, sorted({g for s in SERIES.values() for g in s["metadata"]["genres"]}))
        if url.path == "/api/v1/collections":
            return self.send(200, page(COLLECTIONS, query))
        if url.path == "/api/v1/readlists":
            return self.send(200, page(READLISTS, query))
        if len(parts) >= 4 and parts[:3] == ["api", "v1", "series"]:
            series = SERIES.get(parts[3])
            if not series:
                return self.send(404, {})
            if len(parts) == 4:
                return self.send(200, public(series))
            if parts[4] == "thumbnail":
                return self.send(200, cover(BOOKS[series["id"] + "-1"], self.cache), "image/png")
        if len(parts) >= 4 and parts[:3] == ["api", "v1", "books"]:
            book = BOOKS.get(parts[3])
            if not book:
                return self.send(404, {})
            if len(parts) == 4:
                return self.send(200, public(book))
            if parts[4] == "thumbnail":
                return self.send(200, cover(book, self.cache), "image/png")
        if len(parts) == 5 and parts[:3] == ["api", "v1", "collections"]:
            collection = next((c for c in COLLECTIONS if c["id"] == parts[3]), None)
            if collection and parts[4] == "series":
                return self.send(200, page([public(SERIES[i]) for i in collection["seriesIds"]], query))
        if len(parts) == 4 and parts[:3] == ["api", "v1", "collections"]:
            collection = next((c for c in COLLECTIONS if c["id"] == parts[3]), None)
            return self.send(200, collection) if collection else self.send(404, {})
        if len(parts) == 5 and parts[:3] == ["api", "v1", "readlists"]:
            readlist = next((r for r in READLISTS if r["id"] == parts[3]), None)
            if readlist and parts[4] == "books":
                return self.send(200, page([public(BOOKS[i]) for i in readlist["bookIds"]], query))
        if len(parts) == 4 and parts[:3] == ["api", "v1", "readlists"]:
            readlist = next((r for r in READLISTS if r["id"] == parts[3]), None)
            return self.send(200, readlist) if readlist else self.send(404, {})
        return self.send(404, {"error": "not in the demo"})

    def do_POST(self):
        url = urlparse(self.path)
        query = parse_qs(url.query)
        body = self.body()
        if url.path == "/api/v1/series/list":
            library = condition_value(body, "libraryId")
            items = [public(s) for s in SERIES.values() if not library or s["libraryId"] == library]
            items.sort(key=lambda s: s["metadata"]["title"].lower())
            return self.send(200, page(items, query))
        if url.path == "/api/v1/books/list":
            series = condition_value(body, "seriesId")
            library = condition_value(body, "libraryId")
            text = (body.get("fullTextSearch") or "").lower()
            items = [public(b) for b in BOOKS.values()
                     if (not series or b["seriesId"] == series) and (not library or b["libraryId"] == library)
                     and (not text or text in b["metadata"]["title"].lower())]
            items.sort(key=lambda b: (b["seriesId"], b["metadata"]["numberSort"]))
            return self.send(200, page(items, query))
        return self.send(404, {"error": "not in the demo"})

    def do_PATCH(self):
        self.body()
        self.send(204, b"")


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip().splitlines()[2])
    Handler.cache = sys.argv[2]
    os.makedirs(Handler.cache, exist_ok=True)
    server = ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), Handler)
    print(f"demo Komga on http://127.0.0.1:{server.server_port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
