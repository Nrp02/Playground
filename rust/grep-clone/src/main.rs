use grep_clone::grep;

fn run(pattern: &str, lines: &[&str]) {
    println!("pattern: {pattern}");
    let matches = grep(pattern, lines);
    if matches.is_empty() {
        println!("  (no matches)");
    } else {
        for (lineno, line) in matches {
            println!("  {lineno}:{line}");
        }
    }
    println!();
}

fn main() {
    let buffer = [
        "fn main() {",
        "    let x = 42;",
        "    println!(\"value is {}\", x);",
        "    if x > 0 {",
        "        warn!(\"positive value\");",
        "    } else {",
        "        error!(\"non-positive value\");",
        "    }",
        "}",
        "use std::collections::HashMap;",
        "aaab",
        "abbbb",
        "cat",
        "coat",
        "ct",
        "12345 apples",
        "no digits here",
    ];

    run("fn", &buffer);
    run("x.=", &buffer);
    run("ab*c", &buffer);
    run("co?at", &buffer);
    run("[0-9]+", &buffer);
    run("^use", &buffer);
    run("\\}$", &buffer);
    run("warn|error", &buffer);
    run("(ab)+", &buffer);
    run("[^0-9 ]+", &buffer);
}
