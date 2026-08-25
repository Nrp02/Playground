use grep_clone::{grep, Regex};

#[test]
fn literal_full_match() {
    let re = Regex::new("cat").unwrap();
    assert!(re.is_match("cat"));
    assert!(!re.is_match("cats"));
    assert!(!re.is_match("a cat"));
}

#[test]
fn literal_substring_search() {
    let re = Regex::new("cat").unwrap();
    assert!(re.find("cat"));
    assert!(re.find("a cat sat"));
    assert!(!re.find("dog"));
}

#[test]
fn dot_matches_any_char() {
    let re = Regex::new("c.t").unwrap();
    assert!(re.is_match("cat"));
    assert!(re.is_match("cot"));
    assert!(!re.is_match("ct"));
    assert!(!re.is_match("caat"));
}

#[test]
fn star_quantifier_including_zero_width() {
    let re = Regex::new("ab*c").unwrap();
    assert!(re.is_match("ac"));
    assert!(re.is_match("abc"));
    assert!(re.is_match("abbbbbc"));
    assert!(!re.is_match("adc"));

    let empty_star = Regex::new("a*").unwrap();
    assert!(empty_star.is_match(""));
    assert!(empty_star.is_match("aaaa"));
}

#[test]
fn plus_quantifier_requires_at_least_one() {
    let re = Regex::new("ab+c").unwrap();
    assert!(!re.is_match("ac"));
    assert!(re.is_match("abc"));
    assert!(re.is_match("abbbc"));
}

#[test]
fn question_quantifier_zero_or_one() {
    let re = Regex::new("colou?r").unwrap();
    assert!(re.is_match("color"));
    assert!(re.is_match("colour"));
    assert!(!re.is_match("colouur"));
}

#[test]
fn character_class_and_ranges() {
    let re = Regex::new("[0-9]+").unwrap();
    assert!(re.is_match("12345"));
    assert!(!re.is_match("12a45"));
    assert!(!re.is_match(""));

    let mixed = Regex::new("[a-cX]").unwrap();
    assert!(mixed.is_match("a"));
    assert!(mixed.is_match("b"));
    assert!(mixed.is_match("c"));
    assert!(mixed.is_match("X"));
    assert!(!mixed.is_match("d"));
}

#[test]
fn negated_character_class() {
    let re = Regex::new("[^0-9]+").unwrap();
    assert!(re.is_match("abc"));
    assert!(!re.is_match("123"));
    assert!(!re.is_match("a1"));
}

#[test]
fn anchors_start_and_end() {
    let starts_with = Regex::new("^abc").unwrap();
    assert!(starts_with.find("abcdef"));
    assert!(!starts_with.find("xabcdef"));

    let ends_with = Regex::new("xyz$").unwrap();
    assert!(ends_with.find("abcxyz"));
    assert!(!ends_with.find("xyzabc"));

    let both = Regex::new("^abc$").unwrap();
    assert!(both.is_match("abc"));
    assert!(!both.is_match("abcd"));
    assert!(!both.is_match("xabc"));
}

#[test]
fn alternation() {
    let re = Regex::new("cat|dog|bird").unwrap();
    assert!(re.is_match("cat"));
    assert!(re.is_match("dog"));
    assert!(re.is_match("bird"));
    assert!(!re.is_match("fish"));
    assert!(re.find("I have a dog at home"));
}

#[test]
fn grouping_with_quantifiers() {
    let re = Regex::new("(ab)+").unwrap();
    assert!(re.is_match("ab"));
    assert!(re.is_match("ababab"));
    assert!(!re.is_match("aba"));

    let nested = Regex::new("(a(bc)+)+").unwrap();
    assert!(nested.is_match("abc"));
    assert!(nested.is_match("abcbcabc"));
    assert!(!nested.is_match("ab"));
}

#[test]
fn nested_alternation_and_grouping() {
    let re = Regex::new("(foo|bar)(baz|qux)").unwrap();
    assert!(re.is_match("foobaz"));
    assert!(re.is_match("barqux"));
    assert!(re.is_match("fooqux"));
    assert!(!re.is_match("foo"));
    assert!(!re.is_match("bazbar"));
}

#[test]
fn zero_width_question_on_group() {
    let re = Regex::new("(abc)?def").unwrap();
    assert!(re.is_match("def"));
    assert!(re.is_match("abcdef"));
    assert!(!re.is_match("abdef"));
}

#[test]
fn grep_finds_matching_lines_with_correct_numbers() {
    let lines = [
        "the quick brown fox",
        "jumps over the lazy dog",
        "pack my box with five dozen liquor jugs",
        "the end",
    ];
    let matches = grep("dog", &lines);
    assert_eq!(matches, vec![(2, "jumps over the lazy dog")]);
}

#[test]
fn grep_supports_regex_constructs_across_multiple_lines() {
    let lines = [
        "error: disk full",
        "warning: low memory",
        "info: startup complete",
        "error: connection refused",
    ];
    let matches = grep("^(error|warning):", &lines);
    assert_eq!(
        matches,
        vec![
            (1, "error: disk full"),
            (2, "warning: low memory"),
            (4, "error: connection refused"),
        ]
    );
}

#[test]
fn grep_reports_no_matches_case() {
    let lines = ["alpha", "beta", "gamma"];
    let matches = grep("zzz", &lines);
    assert!(matches.is_empty());
}

#[test]
fn grep_digit_class_search() {
    let lines = ["room 101", "no numbers here", "year 2024 report"];
    let matches = grep("[0-9]+", &lines);
    assert_eq!(matches, vec![(1, "room 101"), (3, "year 2024 report")]);
}
