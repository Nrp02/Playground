'use strict';

/**
 * Unit tests for markdown-parser.js — no test framework, just Node's
 * built-in `assert` module. Run with: node markdown-parser.test.js
 */

const assert = require('assert');
const { parseMarkdown } = require('./markdown-parser.js');

let passCount = 0;
let failCount = 0;

/**
 * Tiny test runner: runs `fn`, catches assertion errors so one failing
 * test doesn't stop the rest of the suite, and logs a result line.
 */
function test(name, fn) {
  try {
    fn();
    passCount++;
    console.log(`  PASS  ${name}`);
  } catch (err) {
    failCount++;
    console.log(`  FAIL  ${name}`);
    console.log(`        ${err.message}`);
  }
}

console.log('Running markdown-parser.js test suite...\n');

// ---------------------------------------------------------------------------
// Headings
// ---------------------------------------------------------------------------

test('parses an h1 through h6', () => {
  assert.strictEqual(parseMarkdown('# H1'), '<h1>H1</h1>');
  assert.strictEqual(parseMarkdown('## H2'), '<h2>H2</h2>');
  assert.strictEqual(parseMarkdown('### H3'), '<h3>H3</h3>');
  assert.strictEqual(parseMarkdown('#### H4'), '<h4>H4</h4>');
  assert.strictEqual(parseMarkdown('##### H5'), '<h5>H5</h5>');
  assert.strictEqual(parseMarkdown('###### H6'), '<h6>H6</h6>');
});

test('seven or more leading #s is not a heading', () => {
  assert.strictEqual(parseMarkdown('####### Not a heading'), '<p>####### Not a heading</p>');
});

test('a "#" with no following space is not a heading', () => {
  assert.strictEqual(parseMarkdown('#NotAHeading'), '<p>#NotAHeading</p>');
});

test('a bare "#" with nothing after it is an empty heading', () => {
  assert.strictEqual(parseMarkdown('#'), '<h1></h1>');
});

test('heading text supports inline markdown', () => {
  assert.strictEqual(parseMarkdown('## Hello **World**'), '<h2>Hello <strong>World</strong></h2>');
});

test('a trailing closing hash sequence is stripped from headings', () => {
  assert.strictEqual(parseMarkdown('# Title #'), '<h1>Title</h1>');
  assert.strictEqual(parseMarkdown('## Title ##'), '<h2>Title</h2>');
});

test('up to 3 leading spaces still count as a heading', () => {
  assert.strictEqual(parseMarkdown('   # Indented'), '<h1>Indented</h1>');
});

// ---------------------------------------------------------------------------
// Emphasis: bold / italic
// ---------------------------------------------------------------------------

test('double-asterisk bold', () => {
  assert.strictEqual(parseMarkdown('**bold**'), '<p><strong>bold</strong></p>');
});

test('double-underscore bold', () => {
  assert.strictEqual(parseMarkdown('__bold__'), '<p><strong>bold</strong></p>');
});

test('single-asterisk italic', () => {
  assert.strictEqual(parseMarkdown('*italic*'), '<p><em>italic</em></p>');
});

test('single-underscore italic', () => {
  assert.strictEqual(parseMarkdown('_italic_'), '<p><em>italic</em></p>');
});

test('triple-asterisk bold+italic', () => {
  assert.strictEqual(parseMarkdown('***both***'), '<p><strong><em>both</em></strong></p>');
});

test('triple-underscore bold+italic', () => {
  assert.strictEqual(parseMarkdown('___both___'), '<p><strong><em>both</em></strong></p>');
});

test('bold and italic can appear side by side', () => {
  assert.strictEqual(
    parseMarkdown('**bold** and *italic*'),
    '<p><strong>bold</strong> and <em>italic</em></p>'
  );
});

test('italic nested inside bold resolves both tags', () => {
  assert.strictEqual(
    parseMarkdown('**bold *and italic* inside**'),
    '<p><strong>bold <em>and italic</em> inside</strong></p>'
  );
});

test('unclosed bold is left as literal asterisks, does not throw', () => {
  assert.strictEqual(parseMarkdown('**never closed'), '<p>**never closed</p>');
});

test('unclosed italic is left as a literal asterisk, does not throw', () => {
  assert.strictEqual(parseMarkdown('*never closed'), '<p>*never closed</p>');
});

test('a lone underscore does not crash or become emphasis', () => {
  assert.doesNotThrow(() => parseMarkdown('_'));
  assert.strictEqual(parseMarkdown('_'), '<p>_</p>');
});

test('underscores inside a word are not treated as emphasis', () => {
  assert.strictEqual(parseMarkdown('snake_case_word'), '<p>snake_case_word</p>');
});

test('asterisks used for multiplication-like text do not become emphasis', () => {
  assert.strictEqual(parseMarkdown('5 * 3 = 15'), '<p>5 * 3 = 15</p>');
});

// ---------------------------------------------------------------------------
// Inline code
// ---------------------------------------------------------------------------

test('inline code span', () => {
  assert.strictEqual(parseMarkdown('Use `parseMarkdown()` here'), '<p>Use <code>parseMarkdown()</code> here</p>');
});

test('markdown syntax inside inline code is not processed', () => {
  assert.strictEqual(parseMarkdown('`**not bold**`'), '<p><code>**not bold**</code></p>');
});

test('HTML inside inline code is escaped', () => {
  assert.strictEqual(parseMarkdown('`<b>`'), '<p><code>&lt;b&gt;</code></p>');
});

test('multiple code spans on one line are each matched independently', () => {
  assert.strictEqual(parseMarkdown('`a` and `b`'), '<p><code>a</code> and <code>b</code></p>');
});

// ---------------------------------------------------------------------------
// Fenced code blocks
// ---------------------------------------------------------------------------

test('fenced code block without a language', () => {
  assert.strictEqual(parseMarkdown('```\nplain text\n```'), '<pre><code>plain text</code></pre>');
});

test('fenced code block preserves the language as a class', () => {
  assert.strictEqual(
    parseMarkdown('```js\nconst x = 1;\n```'),
    '<pre><code class="language-js">const x = 1;</code></pre>'
  );
});

test('fenced code block content is HTML-escaped', () => {
  assert.strictEqual(
    parseMarkdown('```html\n<script>alert(1)</script>\n```'),
    '<pre><code class="language-html">&lt;script&gt;alert(1)&lt;/script&gt;</code></pre>'
  );
});

test('fenced code block preserves multiple lines and blank lines inside it', () => {
  const out = parseMarkdown('```\nline1\n\nline3\n```');
  assert.strictEqual(out, '<pre><code>line1\n\nline3</code></pre>');
});

test('markdown syntax inside a fenced code block is not processed', () => {
  const out = parseMarkdown('```\n# not a heading\n**not bold**\n```');
  assert.strictEqual(out, '<pre><code># not a heading\n**not bold**</code></pre>');
});

test('an unterminated fence is lenient and consumes to end of input without throwing', () => {
  assert.doesNotThrow(() => parseMarkdown('```\nunterminated'));
  assert.strictEqual(parseMarkdown('```\nunterminated'), '<pre><code>unterminated</code></pre>');
});

test('tilde fences are also supported', () => {
  assert.strictEqual(
    parseMarkdown('~~~py\nprint(1)\n~~~'),
    '<pre><code class="language-py">print(1)</code></pre>'
  );
});

// ---------------------------------------------------------------------------
// Links and images
// ---------------------------------------------------------------------------

test('a basic link', () => {
  assert.strictEqual(
    parseMarkdown('[Anthropic](https://anthropic.com)'),
    '<p><a href="https://anthropic.com">Anthropic</a></p>'
  );
});

test('a link with a title', () => {
  assert.strictEqual(
    parseMarkdown('[Anthropic](https://anthropic.com "AI safety company")'),
    '<p><a href="https://anthropic.com" title="AI safety company">Anthropic</a></p>'
  );
});

test('a basic image', () => {
  assert.strictEqual(
    parseMarkdown('![a cat](https://example.com/cat.png)'),
    '<p><img src="https://example.com/cat.png" alt="a cat"></p>'
  );
});

test('an image with a title', () => {
  assert.strictEqual(
    parseMarkdown('![a cat](https://example.com/cat.png "A cute cat")'),
    '<p><img src="https://example.com/cat.png" alt="a cat" title="A cute cat"></p>'
  );
});

test('image syntax takes priority over link syntax so it renders as <img>, not <a><img></a>', () => {
  const out = parseMarkdown('![alt](https://example.com/a.png)');
  assert.ok(out.includes('<img'));
  assert.ok(!out.includes('<a '));
});

test('link and image URLs are HTML-escaped as attributes', () => {
  const out = parseMarkdown('[x](https://example.com/?a=1&b=2)');
  assert.ok(out.includes('href="https://example.com/?a=1&amp;b=2"'));
});

test('link text is HTML-escaped', () => {
  const out = parseMarkdown('[<script>](https://example.com)');
  assert.ok(out.includes('&lt;script&gt;'));
  assert.ok(!out.includes('<script>'));
});

// ---------------------------------------------------------------------------
// Horizontal rules
// ---------------------------------------------------------------------------

test('three or more hyphens is a horizontal rule', () => {
  assert.strictEqual(parseMarkdown('---'), '<hr>');
  assert.strictEqual(parseMarkdown('-----'), '<hr>');
});

test('three or more asterisks is a horizontal rule', () => {
  assert.strictEqual(parseMarkdown('***'), '<hr>');
});

test('three or more underscores is a horizontal rule', () => {
  assert.strictEqual(parseMarkdown('___'), '<hr>');
});

test('space-separated hr characters still count', () => {
  assert.strictEqual(parseMarkdown('- - -'), '<hr>');
});

test('two hyphens alone is not a horizontal rule', () => {
  assert.notStrictEqual(parseMarkdown('--'), '<hr>');
});

test('a horizontal rule separates surrounding paragraphs', () => {
  assert.strictEqual(parseMarkdown('above\n\n---\n\nbelow'), '<p>above</p>\n<hr>\n<p>below</p>');
});

// ---------------------------------------------------------------------------
// Paragraphs and line breaks
// ---------------------------------------------------------------------------

test('a single line of text becomes a paragraph', () => {
  assert.strictEqual(parseMarkdown('Just some text.'), '<p>Just some text.</p>');
});

test('a blank line separates two paragraphs', () => {
  assert.strictEqual(parseMarkdown('First.\n\nSecond.'), '<p>First.</p>\n<p>Second.</p>');
});

test('soft-wrapped lines within a paragraph join with a space', () => {
  assert.strictEqual(parseMarkdown('line one\nline two'), '<p>line one line two</p>');
});

test('a trailing double space forces a hard line break', () => {
  assert.strictEqual(parseMarkdown('line one  \nline two'), '<p>line one<br> line two</p>');
});

test('a trailing backslash forces a hard line break', () => {
  assert.strictEqual(parseMarkdown('line one\\\nline two'), '<p>line one<br> line two</p>');
});

test('empty input produces empty output', () => {
  assert.strictEqual(parseMarkdown(''), '');
});

test('null and undefined input do not throw and produce empty output', () => {
  assert.doesNotThrow(() => parseMarkdown(null));
  assert.doesNotThrow(() => parseMarkdown(undefined));
  assert.strictEqual(parseMarkdown(null), '');
  assert.strictEqual(parseMarkdown(undefined), '');
});

test('input that is only whitespace/blank lines produces empty output', () => {
  assert.strictEqual(parseMarkdown('\n\n   \n\t\n'), '');
});

// ---------------------------------------------------------------------------
// HTML escaping / injection safety
// ---------------------------------------------------------------------------

test('a raw <script> tag in paragraph text is escaped, not executed as markup', () => {
  const out = parseMarkdown('<script>alert(1)</script>');
  assert.strictEqual(out, '<p>&lt;script&gt;alert(1)&lt;/script&gt;</p>');
  assert.ok(!out.includes('<script>'));
});

test('a raw <script> tag inside a list item is escaped', () => {
  const out = parseMarkdown('- <script>alert(1)</script>');
  assert.ok(out.includes('&lt;script&gt;'));
  assert.ok(!out.includes('<script>'));
});

test('a raw <script> tag inside a blockquote is escaped', () => {
  const out = parseMarkdown('> <script>alert(1)</script>');
  assert.ok(out.includes('&lt;script&gt;'));
  assert.ok(!out.includes('<script>'));
});

test('ampersands, quotes, and angle brackets are all escaped in plain text', () => {
  assert.strictEqual(parseMarkdown('Tom & Jerry <3 "quotes" \'apostrophe\''), '<p>Tom &amp; Jerry &lt;3 &quot;quotes&quot; &#39;apostrophe&#39;</p>');
});

test('escaping happens even when combined with real emphasis on the same line', () => {
  const out = parseMarkdown('**bold** <img src=x onerror=alert(1)>');
  assert.ok(out.includes('<strong>bold</strong>'));
  assert.ok(out.includes('&lt;img src=x onerror=alert(1)&gt;'));
});

// ---------------------------------------------------------------------------
// Unordered lists
// ---------------------------------------------------------------------------

test('a simple unordered list with hyphen bullets', () => {
  assert.strictEqual(
    parseMarkdown('- one\n- two\n- three'),
    '<ul>\n<li>one</li>\n<li>two</li>\n<li>three</li>\n</ul>'
  );
});

test('asterisk and plus bullets also produce unordered lists', () => {
  assert.strictEqual(parseMarkdown('* one\n* two'), '<ul>\n<li>one</li>\n<li>two</li>\n</ul>');
  assert.strictEqual(parseMarkdown('+ one\n+ two'), '<ul>\n<li>one</li>\n<li>two</li>\n</ul>');
});

test('list item text supports inline markdown', () => {
  assert.strictEqual(parseMarkdown('- **bold** item'), '<ul>\n<li><strong>bold</strong> item</li>\n</ul>');
});

test('a switch in bullet character starts a new list', () => {
  const out = parseMarkdown('- one\n- two\n* three');
  // Two separate <ul> blocks rather than one five-item list.
  const ulCount = (out.match(/<ul>/g) || []).length;
  assert.strictEqual(ulCount, 2);
});

// ---------------------------------------------------------------------------
// Ordered lists
// ---------------------------------------------------------------------------

test('a simple ordered list', () => {
  assert.strictEqual(
    parseMarkdown('1. one\n2. two\n3. three'),
    '<ol>\n<li>one</li>\n<li>two</li>\n<li>three</li>\n</ol>'
  );
});

test('ordered list starting at a number other than 1 carries a start attribute', () => {
  const out = parseMarkdown('5. five\n6. six');
  assert.ok(out.startsWith('<ol start="5">'));
});

test('ordered list starting at 1 has no start attribute', () => {
  const out = parseMarkdown('1. one\n2. two');
  assert.ok(out.startsWith('<ol>'));
  assert.ok(!out.includes('start='));
});

test('ordered lists support the ")" delimiter as well as "."', () => {
  assert.strictEqual(parseMarkdown('1) one\n2) two'), '<ol>\n<li>one</li>\n<li>two</li>\n</ol>');
});

// ---------------------------------------------------------------------------
// Nested lists
// ---------------------------------------------------------------------------

test('an unordered list nested inside an unordered list', () => {
  const out = parseMarkdown('- a\n- b\n  - nested1\n  - nested2\n- c');
  assert.ok(out.includes('<ul>'));
  // The nested <ul> should appear before the outer list's "c" item, proving
  // it landed inside "b"'s <li> rather than after the whole outer list.
  const bIndex = out.indexOf('<p>b</p>');
  const nestedUlIndex = out.indexOf('<li>nested1</li>');
  const cIndex = out.indexOf('<li>c</li>');
  assert.ok(bIndex !== -1, 'expected item "b" to be wrapped as its own paragraph since it has nested content');
  assert.ok(bIndex < nestedUlIndex && nestedUlIndex < cIndex, 'nested list should sit between b and c');
});

test('an ordered list nested inside an unordered list', () => {
  const out = parseMarkdown('- fruits\n  1. apple\n  2. banana\n- veggies');
  assert.ok(/<ul>[\s\S]*<ol>[\s\S]*<li>apple<\/li>[\s\S]*<\/ol>[\s\S]*<\/ul>/.test(out));
});

test('an unordered list nested inside an ordered list', () => {
  const out = parseMarkdown('1. First\n   - nested a\n   - nested b\n2. Second');
  assert.ok(out.includes('<ol>'));
  assert.ok(out.includes('<ul>'));
  const firstLi = out.indexOf('First');
  const nestedUl = out.indexOf('<ul>');
  const secondLi = out.indexOf('Second');
  assert.ok(firstLi < nestedUl && nestedUl < secondLi);
});

test('two levels of nesting stay correctly contained', () => {
  const out = parseMarkdown('- level1\n  - level2\n    - level3');
  const ulCount = (out.match(/<ul>/g) || []).length;
  assert.strictEqual(ulCount, 3);
  assert.ok(out.includes('<li>level3</li>'));
});

test('a tight list (no nested content) does not wrap items in <p>', () => {
  const out = parseMarkdown('- one\n- two');
  assert.ok(!out.includes('<p>'));
});

// ---------------------------------------------------------------------------
// Blockquotes
// ---------------------------------------------------------------------------

test('a simple blockquote', () => {
  assert.strictEqual(parseMarkdown('> quoted text'), '<blockquote>\n<p>quoted text</p>\n</blockquote>');
});

test('a multi-line blockquote joins into one paragraph', () => {
  assert.strictEqual(
    parseMarkdown('> line one\n> line two'),
    '<blockquote>\n<p>line one line two</p>\n</blockquote>'
  );
});

test('blockquote content supports inline markdown', () => {
  assert.strictEqual(
    parseMarkdown('> **bold** quote'),
    '<blockquote>\n<p><strong>bold</strong> quote</p>\n</blockquote>'
  );
});

test('nested blockquotes produce nested <blockquote> tags', () => {
  const out = parseMarkdown('> outer\n> > inner');
  const firstOpen = out.indexOf('<blockquote>');
  const secondOpen = out.indexOf('<blockquote>', firstOpen + 1);
  const firstClose = out.indexOf('</blockquote>');
  const secondClose = out.lastIndexOf('</blockquote>');
  assert.ok(secondOpen !== -1, 'expected two <blockquote> opening tags');
  assert.ok(secondOpen < firstClose, 'inner blockquote should open before the outer one closes');
  assert.ok(firstClose < secondClose, 'outer close tag should be the last one');
});

test('a blockquote can contain a list', () => {
  const out = parseMarkdown('> - item a\n> - item b');
  assert.ok(out.includes('<blockquote>'));
  assert.ok(out.includes('<ul>'));
  assert.ok(out.includes('<li>item a</li>'));
});

test('a blockquote ends where quote markers stop', () => {
  const out = parseMarkdown('> quoted\n\nnot quoted');
  assert.ok(out.includes('<blockquote>'));
  assert.ok(out.includes('</blockquote>'));
  assert.ok(out.indexOf('not quoted') > out.indexOf('</blockquote>'));
});

// ---------------------------------------------------------------------------
// Mixed / integration-style checks
// ---------------------------------------------------------------------------

test('a realistic multi-feature document parses without throwing and contains expected tags', () => {
  const doc = [
    '# Project Notes',
    '',
    'Some **important** text with a [link](https://example.com) and `inline code`.',
    '',
    '## Todo',
    '',
    '- Write docs',
    '  - Sub-task A',
    '  - Sub-task B',
    '- Ship it',
    '',
    '> A relevant quote.',
    '',
    '```js',
    'function greet() { return "hi"; }',
    '```',
    '',
    '---',
    '',
    'Final paragraph.',
  ].join('\n');

  let out;
  assert.doesNotThrow(() => {
    out = parseMarkdown(doc);
  });

  assert.ok(out.includes('<h1>Project Notes</h1>'));
  assert.ok(out.includes('<h2>Todo</h2>'));
  assert.ok(out.includes('<strong>important</strong>'));
  assert.ok(out.includes('<a href="https://example.com">link</a>'));
  assert.ok(out.includes('<code>inline code</code>'));
  assert.ok(out.includes('<ul>'));
  assert.ok(out.includes('Sub-task A'));
  assert.ok(out.includes('<blockquote>'));
  assert.ok(out.includes('<pre><code class="language-js">'));
  assert.ok(out.includes('<hr>'));
  assert.ok(out.includes('Final paragraph.'));
});

test('parseMarkdown never throws on a grab-bag of malformed/edge-case input', () => {
  const inputs = [
    '**', '***', '____', '``', '[]()', '![]()', '[text](', '> > > > deep',
    '#'.repeat(20), '-'.repeat(3) + 'x', '1.', '- ', '\t\t\ttabbed', '```',
    '[a](b "c', 'a_b_c_d_e',
  ];
  inputs.forEach((input) => {
    assert.doesNotThrow(() => parseMarkdown(input), `should not throw on: ${JSON.stringify(input)}`);
  });
});

test('consecutive different block types in one document each get their own tag', () => {
  const out = parseMarkdown('# Heading\nParagraph text\n- list item\n> quote');
  assert.ok(out.includes('<h1>Heading</h1>'));
  assert.ok(out.includes('<p>Paragraph text</p>'));
  assert.ok(out.includes('<li>list item</li>'));
  assert.ok(out.includes('<blockquote>'));
});

// ---------------------------------------------------------------------------
// Summary
// ---------------------------------------------------------------------------

console.log(`\n${passCount} passed, ${failCount} failed (${passCount + failCount} total)`);

if (failCount > 0) {
  process.exitCode = 1;
} else {
  console.log('All assertions passed.');
}
