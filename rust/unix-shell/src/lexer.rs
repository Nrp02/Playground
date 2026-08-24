#[derive(Debug, Clone, PartialEq)]
pub enum WordPart {
    Literal(String),
    Var(String),
}

#[derive(Debug, Clone, PartialEq)]
pub enum Token {
    Word(Vec<WordPart>),
    Pipe,
    And,
    Or,
    Semi,
    Amp,
    Redirect { fd: i32, append: bool },
    RedirectIn,
}

pub fn lex(input: &str) -> Result<Vec<Token>, String> {
    let chars: Vec<char> = input.chars().collect();
    let mut i = 0usize;
    let mut tokens = Vec::new();

    while i < chars.len() {
        let c = chars[i];

        if c == ' ' || c == '\t' {
            i += 1;
            continue;
        }
        if c == '#' {
            while i < chars.len() && chars[i] != '\n' {
                i += 1;
            }
            continue;
        }
        if c == '\n' {
            tokens.push(Token::Semi);
            i += 1;
            continue;
        }
        if c == '|' {
            if i + 1 < chars.len() && chars[i + 1] == '|' {
                tokens.push(Token::Or);
                i += 2;
            } else {
                tokens.push(Token::Pipe);
                i += 1;
            }
            continue;
        }
        if c == '&' {
            if i + 1 < chars.len() && chars[i + 1] == '&' {
                tokens.push(Token::And);
                i += 2;
            } else {
                tokens.push(Token::Amp);
                i += 1;
            }
            continue;
        }
        if c == ';' {
            tokens.push(Token::Semi);
            i += 1;
            continue;
        }
        if c == '>' {
            if i + 1 < chars.len() && chars[i + 1] == '>' {
                tokens.push(Token::Redirect { fd: 1, append: true });
                i += 2;
            } else {
                tokens.push(Token::Redirect { fd: 1, append: false });
                i += 1;
            }
            continue;
        }
        if c == '<' {
            tokens.push(Token::RedirectIn);
            i += 1;
            continue;
        }
        if c.is_ascii_digit() {
            let mut j = i;
            while j < chars.len() && chars[j].is_ascii_digit() {
                j += 1;
            }
            if j < chars.len() && chars[j] == '>' {
                let fd: i32 = chars[i..j].iter().collect::<String>().parse().unwrap_or(1);
                let mut k = j + 1;
                let append = if k < chars.len() && chars[k] == '>' {
                    k += 1;
                    true
                } else {
                    false
                };
                tokens.push(Token::Redirect { fd, append });
                i = k;
                continue;
            }
        }

        let (word, next) = lex_word(&chars, i)?;
        tokens.push(Token::Word(word));
        i = next;
    }

    Ok(tokens)
}

fn is_word_boundary(c: char) -> bool {
    matches!(c, ' ' | '\t' | '\n' | '|' | '&' | ';' | '>' | '<' | '#')
}

fn lex_word(chars: &[char], start: usize) -> Result<(Vec<WordPart>, usize), String> {
    let mut i = start;
    let mut parts: Vec<WordPart> = Vec::new();
    let mut current = String::new();

    while i < chars.len() {
        let c = chars[i];

        if is_word_boundary(c) {
            break;
        }

        if c == '\'' {
            i += 1;
            let seg_start = i;
            while i < chars.len() && chars[i] != '\'' {
                i += 1;
            }
            if i >= chars.len() {
                return Err("unterminated single quote".to_string());
            }
            current.push_str(&chars[seg_start..i].iter().collect::<String>());
            i += 1;
            continue;
        }

        if c == '"' {
            i += 1;
            while i < chars.len() && chars[i] != '"' {
                if chars[i] == '\\'
                    && i + 1 < chars.len()
                    && matches!(chars[i + 1], '"' | '\\' | '$')
                {
                    current.push(chars[i + 1]);
                    i += 2;
                    continue;
                }
                if chars[i] == '$' {
                    if !current.is_empty() {
                        parts.push(WordPart::Literal(std::mem::take(&mut current)));
                    }
                    let (name, next) = read_var_name(chars, i + 1);
                    if name.is_empty() {
                        current.push('$');
                        i += 1;
                    } else {
                        parts.push(WordPart::Var(name));
                        i = next;
                    }
                    continue;
                }
                current.push(chars[i]);
                i += 1;
            }
            if i >= chars.len() {
                return Err("unterminated double quote".to_string());
            }
            i += 1;
            continue;
        }

        if c == '\\' {
            if i + 1 < chars.len() {
                current.push(chars[i + 1]);
                i += 2;
            } else {
                i += 1;
            }
            continue;
        }

        if c == '$' {
            if !current.is_empty() {
                parts.push(WordPart::Literal(std::mem::take(&mut current)));
            }
            let (name, next) = read_var_name(chars, i + 1);
            if name.is_empty() {
                current.push('$');
                i += 1;
            } else {
                parts.push(WordPart::Var(name));
                i = next;
            }
            continue;
        }

        current.push(c);
        i += 1;
    }

    if !current.is_empty() {
        parts.push(WordPart::Literal(current));
    }
    if parts.is_empty() {
        parts.push(WordPart::Literal(String::new()));
    }

    Ok((parts, i))
}

fn read_var_name(chars: &[char], start: usize) -> (String, usize) {
    if start < chars.len() && chars[start] == '{' {
        let mut j = start + 1;
        while j < chars.len() && chars[j] != '}' {
            j += 1;
        }
        let name: String = chars[start + 1..j].iter().collect();
        return (name, (j + 1).min(chars.len()));
    }
    if start < chars.len() && matches!(chars[start], '?' | '$' | '!' | '#') {
        return (chars[start].to_string(), start + 1);
    }
    let mut j = start;
    while j < chars.len() && (chars[j].is_alphanumeric() || chars[j] == '_') {
        j += 1;
    }
    (chars[start..j].iter().collect(), j)
}
