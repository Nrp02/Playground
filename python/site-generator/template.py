import re


def render(source, context):
    text = re.sub(
        r"\{%\s*for\s+(\w+)\s+in\s+(\w+)\s*%\}(.*?)\{%\s*endfor\s*%\}",
        lambda m: render_for_loop(m, context),
        source,
        flags=re.DOTALL,
    )

    def substitute(match):
        expr = match.group(1).strip()
        return str(_resolve(expr, context))

    text = re.sub(r"\{\{\s*(.+?)\s*\}\}", substitute, text)
    return text


def render_for_loop(match, context):
    item_name = match.group(1)
    var_name = match.group(2)
    body = match.group(3)
    items = context.get(var_name, [])
    pieces = []
    for item in items:
        item_context = dict(context)
        item_context[item_name] = item
        pieces.append(render(body, item_context))
    return "".join(pieces)


def _resolve(expr, context):
    parts = expr.split(".")
    value = context.get(parts[0], "")
    for part in parts[1:]:
        if isinstance(value, dict):
            value = value.get(part, "")
        else:
            value = getattr(value, part, "")
    return value
