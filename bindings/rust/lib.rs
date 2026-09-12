//! POSIX awk grammar for the Tree-sitter parsing library.
//!
//! ```
//! let mut parser = tree_sitter::Parser::new();
//! parser
//!     .set_language(&tree_sitter_posix_awk::LANGUAGE.into())
//!     .expect("POSIX awk grammar must load");
//! let tree = parser
//!     .parse("BEGIN { print 1 }\n", None)
//!     .expect("parser must return a tree");
//! assert!(!tree.root_node().has_error());
//! ```

use tree_sitter_language::LanguageFn;

unsafe extern "C" {
  fn tree_sitter_posix_awk() -> *const ();
}

/// The tree-sitter [`LanguageFn`] for this grammar.
pub const LANGUAGE: LanguageFn =
  unsafe { LanguageFn::from_raw(tree_sitter_posix_awk) };

/// The content of the [`node-types.json`] file for this grammar.
///
/// [`node-types.json`]: https://tree-sitter.github.io/tree-sitter/using-parsers/6-static-node-types
pub const NODE_TYPES: &str = include_str!("../../src/node-types.json");

#[cfg(with_highlights_query)]
/// The syntax highlighting query for this grammar.
pub const HIGHLIGHTS_QUERY: &str = include_str!("../../queries/highlights.scm");

#[cfg(with_injections_query)]
/// The language injection query for this grammar.
pub const INJECTIONS_QUERY: &str = include_str!("../../queries/injections.scm");

#[cfg(with_locals_query)]
/// The local variable query for this grammar.
pub const LOCALS_QUERY: &str = include_str!("../../queries/locals.scm");

#[cfg(with_tags_query)]
/// The symbol tagging query for this grammar.
pub const TAGS_QUERY: &str = include_str!("../../queries/tags.scm");

#[cfg(test)]
mod tests {
  #[test]
  fn posix_awk_grammar_loads_and_parses() {
    let source = "BEGIN { print 1 }\n";
    let mut parser = tree_sitter::Parser::new();
    parser
      .set_language(&super::LANGUAGE.into())
      .expect("generated grammar must load");
    let tree = parser
      .parse(source, None)
      .expect("parser must return a tree");
    let root = tree.root_node();
    assert_eq!(root.kind(), "program");
    assert_eq!(root.start_byte(), 0);
    assert_eq!(root.end_byte(), source.len());
    assert!(!root.has_error());
  }
}
