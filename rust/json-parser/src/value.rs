//! The `Value` type: an in-memory representation of a parsed JSON document,
//! plus pretty-printing back to JSON text.

use std::fmt;

/// A parsed JSON value.
///
/// Objects are represented as an ordered `Vec` of key-value pairs rather
/// than a `HashMap`/`BTreeMap` so that key insertion order from the source
/// document is preserved on re-serialization, and so duplicate keys (legal,
/// if discouraged, in the JSON grammar) survive parsing instead of silently
/// clobbering one another.
#[derive(Debug, Clone, PartialEq)]
pub enum Value {
    Null,
    Bool(bool),
    Number(f64),
    String(String),
    Array(Vec<Value>),
    Object(Vec<(String, Value)>),
}

impl Value {
    /// Look up a key in an object value. Returns `None` if this value is
    /// not an object or the key is absent. If the same key appears more
    /// than once, the first occurrence wins.
    pub fn get(&self, key: &str) -> Option<&Value> {
        match self {
            Value::Object(entries) => entries.iter().find(|(k, _)| k == key).map(|(_, v)| v),
            _ => None,
        }
    }

    /// Index into an array value. Returns `None` if this value is not an
    /// array or the index is out of bounds.
    pub fn index(&self, i: usize) -> Option<&Value> {
        match self {
            Value::Array(items) => items.get(i),
            _ => None,
        }
    }

    pub fn is_null(&self) -> bool {
        matches!(self, Value::Null)
    }

    pub fn as_bool(&self) -> Option<bool> {
        match self {
            Value::Bool(b) => Some(*b),
            _ => None,
        }
    }

    pub fn as_f64(&self) -> Option<f64> {
        match self {
            Value::Number(n) => Some(*n),
            _ => None,
        }
    }

    pub fn as_str(&self) -> Option<&str> {
        match self {
            Value::String(s) => Some(s.as_str()),
            _ => None,
        }
    }

    pub fn as_array(&self) -> Option<&[Value]> {
        match self {
            Value::Array(items) => Some(items.as_slice()),
            _ => None,
        }
    }

    pub fn as_object(&self) -> Option<&[(String, Value)]> {
        match self {
            Value::Object(entries) => Some(entries.as_slice()),
            _ => None,
        }
    }

    /// Serialize this value to a human-readable, indented JSON string
    /// (two spaces per nesting level).
    pub fn to_pretty_string(&self) -> String {
        let mut out = String::new();
        self.write_pretty(&mut out, 0);
        out
    }

    /// Serialize this value to the most compact valid JSON string possible
    /// (no insignificant whitespace).
    pub fn to_compact_string(&self) -> String {
        let mut out = String::new();
        self.write_compact(&mut out);
        out
    }

    fn write_pretty(&self, out: &mut String, indent: usize) {
        match self {
            Value::Null => out.push_str("null"),
            Value::Bool(b) => out.push_str(if *b { "true" } else { "false" }),
            Value::Number(n) => out.push_str(&format_number(*n)),
            Value::String(s) => write_escaped_string(out, s),
            Value::Array(items) => {
                if items.is_empty() {
                    out.push_str("[]");
                    return;
                }
                out.push('[');
                out.push('\n');
                let last = items.len() - 1;
                for (i, item) in items.iter().enumerate() {
                    push_indent(out, indent + 1);
                    item.write_pretty(out, indent + 1);
                    if i != last {
                        out.push(',');
                    }
                    out.push('\n');
                }
                push_indent(out, indent);
                out.push(']');
            }
            Value::Object(entries) => {
                if entries.is_empty() {
                    out.push_str("{}");
                    return;
                }
                out.push('{');
                out.push('\n');
                let last = entries.len() - 1;
                for (i, (key, value)) in entries.iter().enumerate() {
                    push_indent(out, indent + 1);
                    write_escaped_string(out, key);
                    out.push_str(": ");
                    value.write_pretty(out, indent + 1);
                    if i != last {
                        out.push(',');
                    }
                    out.push('\n');
                }
                push_indent(out, indent);
                out.push('}');
            }
        }
    }

    fn write_compact(&self, out: &mut String) {
        match self {
            Value::Null => out.push_str("null"),
            Value::Bool(b) => out.push_str(if *b { "true" } else { "false" }),
            Value::Number(n) => out.push_str(&format_number(*n)),
            Value::String(s) => write_escaped_string(out, s),
            Value::Array(items) => {
                out.push('[');
                for (i, item) in items.iter().enumerate() {
                    if i > 0 {
                        out.push(',');
                    }
                    item.write_compact(out);
                }
                out.push(']');
            }
            Value::Object(entries) => {
                out.push('{');
                for (i, (key, value)) in entries.iter().enumerate() {
                    if i > 0 {
                        out.push(',');
                    }
                    write_escaped_string(out, key);
                    out.push(':');
                    value.write_compact(out);
                }
                out.push('}');
            }
        }
    }
}

/// `Display` renders the pretty-printed form.
impl fmt::Display for Value {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.to_pretty_string())
    }
}

impl From<bool> for Value {
    fn from(b: bool) -> Self {
        Value::Bool(b)
    }
}

impl From<f64> for Value {
    fn from(n: f64) -> Self {
        Value::Number(n)
    }
}

impl From<&str> for Value {
    fn from(s: &str) -> Self {
        Value::String(s.to_string())
    }
}

impl From<String> for Value {
    fn from(s: String) -> Self {
        Value::String(s)
    }
}

fn push_indent(out: &mut String, level: usize) {
    for _ in 0..level {
        out.push_str("  ");
    }
}

/// Format a JSON number for output. Integral values that fit safely in an
/// `i64` are printed without a trailing `.0`; everything else uses Rust's
/// standard shortest-round-trip float formatting.
fn format_number(n: f64) -> String {
    if n.is_finite() && n == n.trunc() && n.abs() < 1e15 {
        format!("{}", n as i64)
    } else {
        format!("{n}")
    }
}

/// Write `s` as a double-quoted JSON string literal into `out`, escaping
/// characters that JSON requires to be escaped.
fn write_escaped_string(out: &mut String, s: &str) {
    out.push('"');
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            '\u{0008}' => out.push_str("\\b"),
            '\u{000C}' => out.push_str("\\f"),
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out.push('"');
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pretty_prints_scalars() {
        assert_eq!(Value::Null.to_pretty_string(), "null");
        assert_eq!(Value::Bool(true).to_pretty_string(), "true");
        assert_eq!(Value::Number(42.0).to_pretty_string(), "42");
        assert_eq!(Value::Number(-3.5).to_pretty_string(), "-3.5");
        assert_eq!(Value::String("hi".into()).to_pretty_string(), "\"hi\"");
    }

    #[test]
    fn pretty_prints_empty_containers_inline() {
        assert_eq!(Value::Array(vec![]).to_pretty_string(), "[]");
        assert_eq!(Value::Object(vec![]).to_pretty_string(), "{}");
    }

    #[test]
    fn escapes_special_characters_in_strings() {
        let s = Value::String("a\"b\\c\nd\te".into());
        assert_eq!(s.to_pretty_string(), "\"a\\\"b\\\\c\\nd\\te\"");
    }

    #[test]
    fn compact_form_has_no_insignificant_whitespace() {
        let v = Value::Object(vec![
            ("a".into(), Value::Number(1.0)),
            ("b".into(), Value::Array(vec![Value::Bool(true), Value::Null])),
        ]);
        assert_eq!(v.to_compact_string(), "{\"a\":1,\"b\":[true,null]}");
    }

    #[test]
    fn get_and_index_accessors() {
        let v = Value::Object(vec![
            ("list".into(), Value::Array(vec![Value::Number(10.0), Value::Number(20.0)])),
        ]);
        let list = v.get("list").unwrap();
        assert_eq!(list.index(1).unwrap().as_f64(), Some(20.0));
        assert!(v.get("missing").is_none());
    }
}
