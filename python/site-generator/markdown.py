import re
import html


def _inline(text):
    text = html.escape(text, quote=False)
    text = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", text)
    text = re.sub(r"(?<!\*)\*(?!\*)(.+?)(?<!\*)\*(?!\*)", r"<em>\1</em>", text)
    text = re.sub(r"`(.+?)`", r"<code>\1</code>", text)
    text = re.sub(r"\[(.+?)\]\((.+?)\)", r'<a href="\2">\1</a>', text)
    return text


def convert(source):
    lines = source.split("\n")
    out = []
    i = 0
    n = len(lines)
    list_open = False

    def close_list():
        nonlocal list_open
        if list_open:
            out.append("</ul>")
            list_open = False

    while i < n:
        line = lines[i]

        if line.startswith("```"):
            close_list()
            code_lines = []
            i += 1
            while i < n and not lines[i].startswith("```"):
                code_lines.append(lines[i])
                i += 1
            code = html.escape("\n".join(code_lines))
            out.append(f"<pre><code>{code}</code></pre>")
            i += 1
            continue

        header_match = re.match(r"^(#{1,6})\s+(.*)$", line)
        if header_match:
            close_list()
            level = len(header_match.group(1))
            out.append(f"<h{level}>{_inline(header_match.group(2))}</h{level}>")
            i += 1
            continue

        list_match = re.match(r"^[-*]\s+(.*)$", line)
        if list_match:
            if not list_open:
                out.append("<ul>")
                list_open = True
            out.append(f"<li>{_inline(list_match.group(1))}</li>")
            i += 1
            continue

        if line.strip() == "":
            close_list()
            i += 1
            continue

        close_list()
        para_lines = [line]
        i += 1
        while i < n and lines[i].strip() != "" and not lines[i].startswith("#") \
                and not re.match(r"^[-*]\s+", lines[i]) and not lines[i].startswith("```"):
            para_lines.append(lines[i])
            i += 1
        out.append(f"<p>{_inline(' '.join(para_lines))}</p>")

    close_list()
    return "\n".join(out)
