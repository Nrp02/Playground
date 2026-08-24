import csv


class Table:
    def __init__(self, name, columns, rows):
        self.name = name
        self.columns = columns
        self.rows = rows


class Database:
    def __init__(self):
        self.tables = {}

    def load_csv(self, name, path):
        with open(path, newline="") as f:
            reader = csv.DictReader(f)
            columns = reader.fieldnames
            rows = []
            for raw in reader:
                row = {}
                for col in columns:
                    row[col] = coerce(raw[col])
                rows.append(row)
        self.tables[name] = Table(name, columns, rows)

    def get_table(self, name):
        if name not in self.tables:
            raise KeyError(f"no such table: {name}")
        return self.tables[name]


def coerce(value):
    try:
        return int(value)
    except ValueError:
        pass
    try:
        return float(value)
    except ValueError:
        pass
    return value
