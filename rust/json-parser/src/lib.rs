//! A JSON lexer, recursive-descent parser, and pretty-printer built from
//! scratch, without depending on `serde` or any JSON crate.
//!
//! ```
//! let value = json_parser::parse(r#"{"a": [1, 2, 3]}"#).unwrap();
//! assert_eq!(value.get("a").unwrap().index(1).unwrap().as_f64(), Some(2.0));
//! println!("{}", value.to_pretty_string());
//! ```

pub mod lexer;
pub mod parser;
pub mod value;

pub use parser::{parse, ParseError};
pub use value::Value;
