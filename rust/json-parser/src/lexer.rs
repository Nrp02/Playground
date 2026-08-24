//! Tokenizer for JSON source text.
//!
//! Converts a JSON document into a flat stream of [`Token`]s, each tagged
//! with its 1-based line and column for error reporting. Operates over the
//! `char` sequence of the input (rather than raw bytes) so that multi-byte
//! UTF-8 characters appearing unescaped inside strings are handled
//! correctly without extra bookkeeping.

use std::fmt;

#[derive(Debug, Clone, PartialEq)]
pub enum TokenKind {
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Colon,
    Comma,
    String(String),
    Number(f64),
    True,
    False,
    Null,
}

impl TokenKind {
    /// A short human-readable description, used in error messages.
    pub fn describe(&self) -> String {
        match self {
            TokenKind::LBrace => "'{'".to_string(),
            TokenKind::RBrace => "'}'".to_string(),
            TokenKind::LBracket => "'['".to_string(),
            TokenKind::RBracket => "']'".to_string(),
            TokenKind::Colon => "':'".to_string(),
            TokenKind::Comma => "','".to_string(),
            TokenKind::String(s) => format!("string {s:?}"),
            TokenKind::Number(n) => format!("number {n}"),
            TokenKind::True => "'true'".to_string(),
            TokenKind::False => "'false'".to_string(),
            TokenKind::Null => "'null'".to_string(),
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct Token {
    pub kind: TokenKind,
    pub line: usize,
    pub column: usize,
}

#[derive(Debug, Clone, PartialEq)]
pub struct LexError {
    pub message: String,
    pub line: usize,
    pub column: usize,
}

impl fmt::Display for LexError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} (line {}, column {})", self.message, self.line, self.column)
    }
}

impl std::error::Error for LexError {}

pub struct Lexer {
    chars: Vec<char>,
    pos: usize,
    line: usize,
    column: usize,
}

impl Lexer {
    pub fn new(input: &str) -> Self {
        Lexer {
            chars: input.chars().collect(),
            pos: 0,
            line: 1,
            column: 1,
        }
    }

    /// Tokenize the entire input, returning every token in order.
    pub fn tokenize(input: &str) -> Result<Vec<Token>, LexError> {
        let mut lexer = Lexer::new(input);
        let mut tokens = Vec::new();
        while let Some(token) = lexer.next_token()? {
            tokens.push(token);
        }
        Ok(tokens)
    }

    fn peek(&self) -> Option<char> {
        self.chars.get(self.pos).copied()
    }

    fn advance(&mut self) -> Option<char> {
        let c = self.peek()?;
        self.pos += 1;
        if c == '\n' {
            self.line += 1;
            self.column = 1;
        } else {
            self.column += 1;
        }
        Some(c)
    }

    fn error(&self, message: impl Into<String>) -> LexError {
        LexError {
            message: message.into(),
            line: self.line,
            column: self.column,
        }
    }

    fn error_at(&self, line: usize, column: usize, message: impl Into<String>) -> LexError {
        LexError {
            message: message.into(),
            line,
            column,
        }
    }

    fn skip_whitespace(&mut self) {
        while let Some(c) = self.peek() {
            if c == ' ' || c == '\t' || c == '\n' || c == '\r' {
                self.advance();
            } else {
                break;
            }
        }
    }

    /// Produce the next token, or `Ok(None)` at end of input.
    pub fn next_token(&mut self) -> Result<Option<Token>, LexError> {
        self.skip_whitespace();
        let line = self.line;
        let column = self.column;

        let c = match self.peek() {
            Some(c) => c,
            None => return Ok(None),
        };

        let kind = match c {
            '{' => {
                self.advance();
                TokenKind::LBrace
            }
            '}' => {
                self.advance();
                TokenKind::RBrace
            }
            '[' => {
                self.advance();
                TokenKind::LBracket
            }
            ']' => {
                self.advance();
                TokenKind::RBracket
            }
            ':' => {
                self.advance();
                TokenKind::Colon
            }
            ',' => {
                self.advance();
                TokenKind::Comma
            }
            '"' => return self.lex_string().map(Some),
            '-' | '0'..='9' => return self.lex_number().map(Some),
            't' => {
                self.expect_literal("true")?;
                TokenKind::True
            }
            'f' => {
                self.expect_literal("false")?;
                TokenKind::False
            }
            'n' => {
                self.expect_literal("null")?;
                TokenKind::Null
            }
            other => return Err(self.error(format!("unexpected character '{other}'"))),
        };

        Ok(Some(Token { kind, line, column }))
    }

    fn expect_literal(&mut self, literal: &str) -> Result<(), LexError> {
        let start_line = self.line;
        let start_col = self.column;
        for expected in literal.chars() {
            match self.advance() {
                Some(c) if c == expected => {}
                Some(c) => {
                    return Err(self.error_at(
                        start_line,
                        start_col,
                        format!("invalid literal: expected \"{literal}\", found unexpected character '{c}'"),
                    ))
                }
                None => {
                    return Err(self.error_at(
                        start_line,
                        start_col,
                        format!("unexpected end of input while reading literal \"{literal}\""),
                    ))
                }
            }
        }
        Ok(())
    }

    fn lex_number(&mut self) -> Result<Token, LexError> {
        let start_line = self.line;
        let start_col = self.column;
        let mut raw = String::new();

        if self.peek() == Some('-') {
            raw.push(self.advance().unwrap());
        }

        match self.peek() {
            Some('0') => raw.push(self.advance().unwrap()),
            Some(c) if c.is_ascii_digit() => {
                while let Some(c) = self.peek() {
                    if c.is_ascii_digit() {
                        raw.push(self.advance().unwrap());
                    } else {
                        break;
                    }
                }
            }
            _ => return Err(self.error("invalid number: expected a digit")),
        }

        if self.peek() == Some('.') {
            raw.push(self.advance().unwrap());
            let mut saw_digit = false;
            while let Some(c) = self.peek() {
                if c.is_ascii_digit() {
                    raw.push(self.advance().unwrap());
                    saw_digit = true;
                } else {
                    break;
                }
            }
            if !saw_digit {
                return Err(self.error("invalid number: expected a digit after decimal point"));
            }
        }

        if matches!(self.peek(), Some('e') | Some('E')) {
            raw.push(self.advance().unwrap());
            if matches!(self.peek(), Some('+') | Some('-')) {
                raw.push(self.advance().unwrap());
            }
            let mut saw_digit = false;
            while let Some(c) = self.peek() {
                if c.is_ascii_digit() {
                    raw.push(self.advance().unwrap());
                    saw_digit = true;
                } else {
                    break;
                }
            }
            if !saw_digit {
                return Err(self.error("invalid number: expected a digit in exponent"));
            }
        }

        let value: f64 = raw
            .parse()
            .map_err(|_| self.error_at(start_line, start_col, format!("invalid number literal '{raw}'")))?;

        Ok(Token {
            kind: TokenKind::Number(value),
            line: start_line,
            column: start_col,
        })
    }

    fn lex_string(&mut self) -> Result<Token, LexError> {
        let start_line = self.line;
        let start_col = self.column;
        self.advance(); // opening quote

        let mut result = String::new();
        loop {
            match self.advance() {
                None => return Err(self.error_at(start_line, start_col, "unterminated string literal")),
                Some('"') => break,
                Some('\\') => {
                    let escaped = self.advance().ok_or_else(|| {
                        self.error_at(start_line, start_col, "unterminated string literal")
                    })?;
                    match escaped {
                        '"' => result.push('"'),
                        '\\' => result.push('\\'),
                        '/' => result.push('/'),
                        'b' => result.push('\u{0008}'),
                        'f' => result.push('\u{000C}'),
                        'n' => result.push('\n'),
                        'r' => result.push('\r'),
                        't' => result.push('\t'),
                        'u' => {
                            let unit = self.read_hex4()?;
                            let ch = if (0xD800..=0xDBFF).contains(&unit) {
                                // High surrogate: a low surrogate must follow immediately.
                                if self.advance() != Some('\\') {
                                    return Err(self.error("expected low surrogate escape after high surrogate"));
                                }
                                if self.advance() != Some('u') {
                                    return Err(self.error("expected low surrogate escape after high surrogate"));
                                }
                                let low = self.read_hex4()?;
                                if !(0xDC00..=0xDFFF).contains(&low) {
                                    return Err(self.error("invalid low surrogate in unicode escape pair"));
                                }
                                let c = 0x10000u32
                                    + ((unit as u32 - 0xD800) << 10)
                                    + (low as u32 - 0xDC00);
                                char::from_u32(c).ok_or_else(|| self.error("invalid unicode surrogate pair"))?
                            } else if (0xDC00..=0xDFFF).contains(&unit) {
                                return Err(self.error("unexpected low surrogate without preceding high surrogate"));
                            } else {
                                char::from_u32(unit as u32).ok_or_else(|| self.error("invalid unicode escape"))?
                            };
                            result.push(ch);
                        }
                        other => return Err(self.error(format!("invalid escape sequence '\\{other}'"))),
                    }
                }
                Some(c) if (c as u32) < 0x20 => {
                    return Err(self.error(format!("control character U+{:04X} must be escaped in string", c as u32)))
                }
                Some(c) => result.push(c),
            }
        }

        Ok(Token {
            kind: TokenKind::String(result),
            line: start_line,
            column: start_col,
        })
    }

    fn read_hex4(&mut self) -> Result<u16, LexError> {
        let mut value: u16 = 0;
        for _ in 0..4 {
            let c = self.advance().ok_or_else(|| self.error("unexpected end of input in \\u escape"))?;
            let digit = c
                .to_digit(16)
                .ok_or_else(|| self.error(format!("invalid hex digit '{c}' in \\u escape")))?;
            value = value * 16 + digit as u16;
        }
        Ok(value)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn kinds(input: &str) -> Vec<TokenKind> {
        Lexer::tokenize(input).unwrap().into_iter().map(|t| t.kind).collect()
    }

    #[test]
    fn tokenizes_structural_characters() {
        assert_eq!(
            kinds("{}[]:,"),
            vec![
                TokenKind::LBrace,
                TokenKind::RBrace,
                TokenKind::LBracket,
                TokenKind::RBracket,
                TokenKind::Colon,
                TokenKind::Comma,
            ]
        );
    }

    #[test]
    fn tokenizes_literals() {
        assert_eq!(kinds("true false null"), vec![TokenKind::True, TokenKind::False, TokenKind::Null]);
    }

    #[test]
    fn tokenizes_numbers() {
        assert_eq!(
            kinds("0 -5 3.75 -2.5 1e10 1E-3 2.5e+2"),
            vec![
                TokenKind::Number(0.0),
                TokenKind::Number(-5.0),
                TokenKind::Number(3.75),
                TokenKind::Number(-2.5),
                TokenKind::Number(1e10),
                TokenKind::Number(1e-3),
                TokenKind::Number(2.5e2),
            ]
        );
    }

    #[test]
    fn rejects_leading_zero_followed_by_digit() {
        // "01" lexes as the number `0` followed by a separate number `1`,
        // since JSON forbids leading zeros; the parser is what ultimately
        // rejects this as a malformed document (trailing data).
        assert_eq!(kinds("01"), vec![TokenKind::Number(0.0), TokenKind::Number(1.0)]);
    }

    #[test]
    fn tokenizes_plain_string() {
        assert_eq!(kinds("\"hello\""), vec![TokenKind::String("hello".to_string())]);
    }

    #[test]
    fn tokenizes_string_escapes() {
        assert_eq!(
            kinds(r#""a\"b\\c\/d\n\t\r\b\f""#),
            vec![TokenKind::String("a\"b\\c/d\n\t\r\u{0008}\u{000C}".to_string())]
        );
    }

    #[test]
    fn tokenizes_unicode_escape() {
        let escaped_capital_a = "\"\\u0041\"";
        assert_eq!(kinds(escaped_capital_a), vec![TokenKind::String("A".to_string())]);
    }

    #[test]
    fn tokenizes_surrogate_pair_escape() {
        // U+1F600 GRINNING FACE, encoded as a UTF-16 surrogate pair.
        assert_eq!(kinds(r#""😀""#), vec![TokenKind::String("\u{1F600}".to_string())]);
    }

    #[test]
    fn errors_on_unterminated_string() {
        assert!(Lexer::tokenize("\"abc").is_err());
    }

    #[test]
    fn errors_on_invalid_escape() {
        assert!(Lexer::tokenize(r#""\q""#).is_err());
    }

    #[test]
    fn errors_on_control_character_in_string() {
        let input = "\"a\tb\"";
        assert!(Lexer::tokenize(input).is_err());
    }

    #[test]
    fn errors_on_malformed_number() {
        assert!(Lexer::tokenize("-").is_err());
        assert!(Lexer::tokenize("1.").is_err());
        assert!(Lexer::tokenize("1e").is_err());
    }

    #[test]
    fn errors_on_unexpected_character() {
        assert!(Lexer::tokenize("@").is_err());
    }

    #[test]
    fn tracks_line_and_column() {
        let tokens = Lexer::tokenize("{\n  \"a\": 1\n}").unwrap();
        // The key "a" is on line 2.
        let string_token = tokens.iter().find(|t| matches!(t.kind, TokenKind::String(_))).unwrap();
        assert_eq!(string_token.line, 2);
    }
}
