//! Recursive-descent parser that builds a [`Value`] tree from a token
//! stream produced by [`crate::lexer::Lexer`].
//!
//! The JSON grammar is small enough that one function per production reads
//! directly off the spec: `parse_value` dispatches on the next token,
//! `parse_object`/`parse_array` each loop over comma-separated members and
//! recurse back into `parse_value` for nested content.

use std::fmt;

use crate::lexer::{LexError, Lexer, Token, TokenKind};
use crate::value::Value;

#[derive(Debug, Clone, PartialEq)]
pub enum ParseError {
    Lex(LexError),
    UnexpectedToken {
        expected: String,
        found: String,
        line: usize,
        column: usize,
    },
    UnexpectedEof {
        expected: String,
    },
    TrailingData {
        found: String,
        line: usize,
        column: usize,
    },
}

impl fmt::Display for ParseError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ParseError::Lex(e) => write!(f, "{e}"),
            ParseError::UnexpectedToken { expected, found, line, column } => {
                write!(f, "expected {expected}, found {found} (line {line}, column {column})")
            }
            ParseError::UnexpectedEof { expected } => {
                write!(f, "unexpected end of input, expected {expected}")
            }
            ParseError::TrailingData { found, line, column } => {
                write!(f, "unexpected trailing data after JSON value: {found} (line {line}, column {column})")
            }
        }
    }
}

impl std::error::Error for ParseError {}

impl From<LexError> for ParseError {
    fn from(e: LexError) -> Self {
        ParseError::Lex(e)
    }
}

/// Parse a complete JSON document from `input`, returning an error if the
/// text is malformed or if anything but whitespace follows the top-level
/// value.
pub fn parse(input: &str) -> Result<Value, ParseError> {
    let tokens = Lexer::tokenize(input)?;
    let mut parser = Parser { tokens, pos: 0 };
    let value = parser.parse_value()?;
    parser.expect_end()?;
    Ok(value)
}

struct Parser {
    tokens: Vec<Token>,
    pos: usize,
}

impl Parser {
    fn peek(&self) -> Option<&Token> {
        self.tokens.get(self.pos)
    }

    fn advance(&mut self) -> Option<Token> {
        let token = self.tokens.get(self.pos).cloned();
        if token.is_some() {
            self.pos += 1;
        }
        token
    }

    fn expect_end(&self) -> Result<(), ParseError> {
        match self.peek() {
            None => Ok(()),
            Some(tok) => Err(ParseError::TrailingData {
                found: tok.kind.describe(),
                line: tok.line,
                column: tok.column,
            }),
        }
    }

    fn parse_value(&mut self) -> Result<Value, ParseError> {
        let token = self
            .peek()
            .cloned()
            .ok_or_else(|| ParseError::UnexpectedEof { expected: "a value".to_string() })?;

        match token.kind {
            TokenKind::LBrace => self.parse_object(),
            TokenKind::LBracket => self.parse_array(),
            TokenKind::String(s) => {
                self.advance();
                Ok(Value::String(s))
            }
            TokenKind::Number(n) => {
                self.advance();
                Ok(Value::Number(n))
            }
            TokenKind::True => {
                self.advance();
                Ok(Value::Bool(true))
            }
            TokenKind::False => {
                self.advance();
                Ok(Value::Bool(false))
            }
            TokenKind::Null => {
                self.advance();
                Ok(Value::Null)
            }
            other => Err(ParseError::UnexpectedToken {
                expected: "a value".to_string(),
                found: other.describe(),
                line: token.line,
                column: token.column,
            }),
        }
    }

    fn parse_object(&mut self) -> Result<Value, ParseError> {
        self.advance(); // consume '{'
        let mut entries = Vec::new();

        if matches!(self.peek().map(|t| &t.kind), Some(TokenKind::RBrace)) {
            self.advance();
            return Ok(Value::Object(entries));
        }

        loop {
            let key_token = self
                .advance()
                .ok_or_else(|| ParseError::UnexpectedEof { expected: "a string key".to_string() })?;
            let key = match key_token.kind {
                TokenKind::String(s) => s,
                other => {
                    return Err(ParseError::UnexpectedToken {
                        expected: "a string key".to_string(),
                        found: other.describe(),
                        line: key_token.line,
                        column: key_token.column,
                    })
                }
            };

            let colon = self
                .advance()
                .ok_or_else(|| ParseError::UnexpectedEof { expected: "':'".to_string() })?;
            if colon.kind != TokenKind::Colon {
                return Err(ParseError::UnexpectedToken {
                    expected: "':'".to_string(),
                    found: colon.kind.describe(),
                    line: colon.line,
                    column: colon.column,
                });
            }

            let value = self.parse_value()?;
            entries.push((key, value));

            let separator = self
                .advance()
                .ok_or_else(|| ParseError::UnexpectedEof { expected: "',' or '}'".to_string() })?;
            match separator.kind {
                TokenKind::Comma => continue,
                TokenKind::RBrace => break,
                other => {
                    return Err(ParseError::UnexpectedToken {
                        expected: "',' or '}'".to_string(),
                        found: other.describe(),
                        line: separator.line,
                        column: separator.column,
                    })
                }
            }
        }

        Ok(Value::Object(entries))
    }

    fn parse_array(&mut self) -> Result<Value, ParseError> {
        self.advance(); // consume '['
        let mut items = Vec::new();

        if matches!(self.peek().map(|t| &t.kind), Some(TokenKind::RBracket)) {
            self.advance();
            return Ok(Value::Array(items));
        }

        loop {
            let value = self.parse_value()?;
            items.push(value);

            let separator = self
                .advance()
                .ok_or_else(|| ParseError::UnexpectedEof { expected: "',' or ']'".to_string() })?;
            match separator.kind {
                TokenKind::Comma => continue,
                TokenKind::RBracket => break,
                other => {
                    return Err(ParseError::UnexpectedToken {
                        expected: "',' or ']'".to_string(),
                        found: other.describe(),
                        line: separator.line,
                        column: separator.column,
                    })
                }
            }
        }

        Ok(Value::Array(items))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_scalars() {
        assert_eq!(parse("null").unwrap(), Value::Null);
        assert_eq!(parse("true").unwrap(), Value::Bool(true));
        assert_eq!(parse("false").unwrap(), Value::Bool(false));
        assert_eq!(parse("42").unwrap(), Value::Number(42.0));
        assert_eq!(parse("-3.5").unwrap(), Value::Number(-3.5));
        assert_eq!(parse("\"hi\"").unwrap(), Value::String("hi".to_string()));
    }

    #[test]
    fn parses_empty_containers() {
        assert_eq!(parse("[]").unwrap(), Value::Array(vec![]));
        assert_eq!(parse("{}").unwrap(), Value::Object(vec![]));
        assert_eq!(parse(" [ ] ").unwrap(), Value::Array(vec![]));
    }

    #[test]
    fn parses_flat_array() {
        assert_eq!(
            parse("[1, 2, 3]").unwrap(),
            Value::Array(vec![Value::Number(1.0), Value::Number(2.0), Value::Number(3.0)])
        );
    }

    #[test]
    fn parses_flat_object() {
        assert_eq!(
            parse(r#"{"a": 1, "b": true}"#).unwrap(),
            Value::Object(vec![
                ("a".to_string(), Value::Number(1.0)),
                ("b".to_string(), Value::Bool(true)),
            ])
        );
    }

    #[test]
    fn parses_nested_structure() {
        let input = r#"{"items": [1, {"nested": true}], "count": 2}"#;
        let parsed = parse(input).unwrap();
        assert_eq!(
            parsed,
            Value::Object(vec![
                (
                    "items".to_string(),
                    Value::Array(vec![
                        Value::Number(1.0),
                        Value::Object(vec![("nested".to_string(), Value::Bool(true))]),
                    ])
                ),
                ("count".to_string(), Value::Number(2.0)),
            ])
        );
    }

    #[test]
    fn whitespace_between_tokens_is_ignored() {
        let a = parse("{\"a\":1,\"b\":2}").unwrap();
        let b = parse("  {  \"a\" : 1 ,  \"b\" : 2  }  ").unwrap();
        assert_eq!(a, b);
    }

    #[test]
    fn errors_on_empty_input() {
        assert!(matches!(parse(""), Err(ParseError::UnexpectedEof { .. })));
    }

    #[test]
    fn errors_on_trailing_comma_in_array() {
        assert!(parse("[1, 2,]").is_err());
    }

    #[test]
    fn errors_on_trailing_comma_in_object() {
        assert!(parse(r#"{"a": 1,}"#).is_err());
    }

    #[test]
    fn errors_on_missing_colon() {
        assert!(parse(r#"{"a" 1}"#).is_err());
    }

    #[test]
    fn errors_on_unquoted_key() {
        assert!(parse("{a: 1}").is_err());
    }

    #[test]
    fn errors_on_mismatched_brackets() {
        assert!(parse("[1, 2}").is_err());
        assert!(parse(r#"{"a": 1]"#).is_err());
    }

    #[test]
    fn errors_on_trailing_data_after_value() {
        assert!(matches!(parse("1 2"), Err(ParseError::TrailingData { .. })));
        assert!(parse("{} {}").is_err());
    }

    #[test]
    fn errors_on_unterminated_object() {
        assert!(parse(r#"{"a": 1"#).is_err());
    }

    #[test]
    fn errors_do_not_panic_on_malformed_input() {
        let malformed_inputs = [
            "{",
            "}",
            "[",
            "]",
            ",",
            ":",
            "{\"a\":}",
            "[,]",
            "tru",
            "nul",
            "\"unterminated",
            "{\"a\": \"b\", }",
        ];
        for input in malformed_inputs {
            assert!(parse(input).is_err(), "expected error for input: {input:?}");
        }
    }
}
