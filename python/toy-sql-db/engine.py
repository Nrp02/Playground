from parser import ColumnRef


def resolve(row, colref):
    if colref.table:
        key = f"{colref.table}.{colref.name}"
        if key in row:
            return row[key]
        raise KeyError(f"no such column: {key}")

    matches = [v for k, v in row.items() if k.split(".", 1)[1] == colref.name]
    if not matches:
        raise KeyError(f"no such column: {colref.name}")
    if len(matches) > 1:
        raise KeyError(f"ambiguous column: {colref.name}")
    return matches[0]


def build_rows(table):
    rows = []
    for r in table.rows:
        rows.append({f"{table.name}.{k}": v for k, v in r.items()})
    return rows


def apply_join(left_rows, right_table, left_col, right_col):
    right_rows = build_rows(right_table)
    joined = []
    for lr in left_rows:
        lv = resolve(lr, left_col)
        for rr in right_rows:
            rv = resolve(rr, right_col)
            if lv == rv:
                merged = dict(lr)
                merged.update(rr)
                joined.append(merged)
    return joined


def compare(a, op, b):
    if op == "=":
        return a == b
    if op == "!=":
        return a != b
    if op == "<":
        return a < b
    if op == ">":
        return a > b
    if op == "<=":
        return a <= b
    if op == ">=":
        return a >= b
    raise ValueError(f"unknown operator: {op}")


def apply_where(rows, conditions):
    if not conditions:
        return rows
    result = []
    for row in rows:
        ok = True
        for cond in conditions:
            left = resolve(row, cond.left)
            right = resolve(row, cond.right) if isinstance(cond.right, ColumnRef) else cond.right
            if not compare(left, cond.op, right):
                ok = False
                break
        if ok:
            result.append(row)
    return result


def apply_order_by(rows, order_by):
    if order_by is None:
        return rows
    return sorted(rows, key=lambda r: resolve(r, order_by.col), reverse=order_by.descending)


def project(rows, star, columns):
    if star:
        if not rows:
            return [], []
        headers = [k.split(".", 1)[1] for k in rows[0].keys()]
        out = [list(r.values()) for r in rows]
        return headers, out

    headers = [str(c) for c in columns]
    out = []
    for r in rows:
        out.append([resolve(r, c) for c in columns])
    return headers, out


def execute(stmt, db):
    table = db.get_table(stmt.from_table)
    rows = build_rows(table)

    if stmt.join:
        right_table = db.get_table(stmt.join.table)
        rows = apply_join(rows, right_table, stmt.join.left_col, stmt.join.right_col)

    rows = apply_where(rows, stmt.where)
    rows = apply_order_by(rows, stmt.order_by)
    headers, out_rows = project(rows, stmt.star, stmt.columns)
    return headers, out_rows
