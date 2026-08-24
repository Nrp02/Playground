import hashlib
import json
import os

import markdown
import template

CACHE_FILE = ".buildcache.json"


def parse_source(path):
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()

    meta = {}
    lines = text.split("\n")
    i = 0
    while i < len(lines) and ":" in lines[i] and lines[i].strip() != "":
        key, _, value = lines[i].partition(":")
        meta[key.strip()] = value.strip()
        i += 1
    while i < len(lines) and lines[i].strip() == "":
        i += 1

    body = "\n".join(lines[i:])
    return meta, body


def file_hash(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def load_cache(cache_path):
    if os.path.exists(cache_path):
        with open(cache_path, "r", encoding="utf-8") as f:
            return json.load(f)
    return {}


def save_cache(cache_path, cache):
    with open(cache_path, "w", encoding="utf-8") as f:
        json.dump(cache, f, indent=2)


def build(content_dir, templates_dir, output_dir):
    cache_path = os.path.join(output_dir, CACHE_FILE)
    os.makedirs(output_dir, exist_ok=True)
    cache = load_cache(cache_path)
    new_cache = {}

    with open(os.path.join(templates_dir, "page.html"), "r", encoding="utf-8") as f:
        page_template = f.read()
    with open(os.path.join(templates_dir, "index.html"), "r", encoding="utf-8") as f:
        index_template = f.read()

    posts = []
    rebuilt = []
    skipped = []

    md_files = sorted(f for f in os.listdir(content_dir) if f.endswith(".md"))

    for filename in md_files:
        src_path = os.path.join(content_dir, filename)
        digest = file_hash(src_path)
        new_cache[filename] = digest

        slug = filename[:-3]
        out_path = os.path.join(output_dir, f"{slug}.html")

        meta, body = parse_source(src_path)
        posts.append({
            "title": meta.get("title", slug),
            "date": meta.get("date", ""),
            "url": f"{slug}.html",
        })

        if cache.get(filename) == digest and os.path.exists(out_path):
            skipped.append(filename)
            continue

        html_body = markdown.convert(body)
        page = template.render(page_template, {
            "title": meta.get("title", slug),
            "date": meta.get("date", ""),
            "content": html_body,
        })
        with open(out_path, "w", encoding="utf-8") as f:
            f.write(page)
        rebuilt.append(filename)

    posts.sort(key=lambda p: p["date"], reverse=True)
    index_page = template.render(index_template, {"posts": posts})
    with open(os.path.join(output_dir, "index.html"), "w", encoding="utf-8") as f:
        f.write(index_page)

    save_cache(cache_path, new_cache)
    return rebuilt, skipped
