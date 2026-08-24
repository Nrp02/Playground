//! Round-trip tests: parse a JSON string, pretty-print it back to text,
//! parse that text again, and assert the two parsed `Value` trees are
//! equal. This exercises the lexer, parser, and pretty-printer together
//! and catches any asymmetry between how a value is written and how it is
//! read back.

use json_parser::{parse, Value};

fn assert_round_trips(input: &str) -> Value {
    let first = parse(input).unwrap_or_else(|e| panic!("failed to parse {input:?}: {e}"));
    let printed = first.to_pretty_string();
    let second = parse(&printed).unwrap_or_else(|e| {
        panic!("failed to re-parse pretty-printed output {printed:?} (from input {input:?}): {e}")
    });
    assert_eq!(first, second, "round-trip mismatch for input {input:?}, printed as {printed:?}");

    let compact = first.to_compact_string();
    let third = parse(&compact).unwrap_or_else(|e| {
        panic!("failed to re-parse compact output {compact:?} (from input {input:?}): {e}")
    });
    assert_eq!(first, third, "compact round-trip mismatch for input {input:?}");

    first
}

#[test]
fn round_trips_null_true_false() {
    assert_round_trips("null");
    assert_round_trips("true");
    assert_round_trips("false");
}

#[test]
fn round_trips_integers() {
    assert_round_trips("0");
    assert_round_trips("42");
    assert_round_trips("-17");
    assert_round_trips("1000000");
}

#[test]
fn round_trips_floats() {
    assert_round_trips("3.75");
    assert_round_trips("-2.5");
    assert_round_trips("0.001");
    assert_round_trips("123.456");
}

#[test]
fn round_trips_numbers_with_exponents() {
    assert_round_trips("1e10");
    assert_round_trips("1.5e-3");
    assert_round_trips("2.5E+2");
    assert_round_trips("-6.022e23");
}

#[test]
fn round_trips_plain_strings() {
    assert_round_trips(r#""hello, world""#);
    assert_round_trips(r#""""#);
    assert_round_trips(r#""with unicode: café""#);
}

#[test]
fn round_trips_strings_with_escapes() {
    let value = assert_round_trips(r#""line1\nline2\ttabbed\\backslash\"quoted\"""#);
    assert_eq!(value.as_str(), Some("line1\nline2\ttabbed\\backslash\"quoted\""));
}

#[test]
fn round_trips_string_with_surrogate_pair_emoji() {
    let value = assert_round_trips(r#""hi 😀 there""#);
    assert_eq!(value.as_str(), Some("hi \u{1F600} there"));
}

#[test]
fn round_trips_empty_array_and_object() {
    assert_round_trips("[]");
    assert_round_trips("{}");
}

#[test]
fn round_trips_flat_array() {
    assert_round_trips("[1, 2, 3, 4, 5]");
    assert_round_trips(r#"["a", "b", "c"]"#);
    assert_round_trips("[true, false, null, 1, \"mixed\"]");
}

#[test]
fn round_trips_flat_object() {
    assert_round_trips(r#"{"name": "Ada", "age": 36, "active": true}"#);
}

#[test]
fn round_trips_nested_object_and_array() {
    let input = r#"
    {
        "id": 1,
        "name": "root",
        "children": [
            {"id": 2, "name": "child-a", "tags": ["x", "y"]},
            {"id": 3, "name": "child-b", "tags": []}
        ],
        "metadata": {
            "created": "2024-01-01",
            "flags": {"archived": false, "pinned": true}
        }
    }
    "#;
    let value = assert_round_trips(input);
    assert_eq!(value.get("id").unwrap().as_f64(), Some(1.0));
    assert_eq!(
        value.get("children").unwrap().index(1).unwrap().get("name").unwrap().as_str(),
        Some("child-b")
    );
    assert_eq!(
        value.get("metadata").unwrap().get("flags").unwrap().get("pinned").unwrap().as_bool(),
        Some(true)
    );
}

#[test]
fn round_trips_deeply_nested_arrays() {
    assert_round_trips("[[[[[1, 2], [3]], []], [4]], [5, 6]]");
}

#[test]
fn round_trips_array_of_objects_of_arrays() {
    let input = r#"[{"a": [1, 2, {"b": [3, 4]}]}, {"c": []}]"#;
    assert_round_trips(input);
}

#[test]
fn round_trip_preserves_key_order() {
    let value = parse(r#"{"z": 1, "a": 2, "m": 3}"#).unwrap();
    let keys: Vec<&str> = value.as_object().unwrap().iter().map(|(k, _)| k.as_str()).collect();
    assert_eq!(keys, vec!["z", "a", "m"]);

    let printed = value.to_pretty_string();
    let reparsed = parse(&printed).unwrap();
    let keys_after: Vec<&str> = reparsed.as_object().unwrap().iter().map(|(k, _)| k.as_str()).collect();
    assert_eq!(keys_after, vec!["z", "a", "m"]);
}

#[test]
fn whitespace_variations_parse_to_equal_values() {
    let compact = parse(r#"{"a":[1,2,3],"b":true}"#).unwrap();
    let spaced = parse(
        r#"
        {
            "a" : [ 1 , 2 , 3 ] ,
            "b" : true
        }
        "#,
    )
    .unwrap();
    assert_eq!(compact, spaced);
}

#[test]
fn malformed_input_returns_err_not_panic() {
    let bad_inputs = [
        "",
        "{",
        "[",
        "{\"a\":}",
        "[1,]",
        "{\"a\": 1,}",
        "{\"a\" 1}",
        "nul",
        "\"unterminated",
        "01",
        "1.2.3",
        "{\"a\": 1} extra",
    ];
    for input in bad_inputs {
        let result = parse(input);
        assert!(result.is_err(), "expected parse error for input {input:?}, got {result:?}");
    }
}

#[test]
fn pretty_printed_output_is_indented() {
    let value = parse(r#"{"a": [1, 2]}"#).unwrap();
    let printed = value.to_pretty_string();
    assert!(printed.contains('\n'));
    assert!(printed.contains("  \"a\""));
}
