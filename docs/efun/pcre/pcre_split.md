---
title: pcre / pcre_split
---
# pcre_split

### NAME

pcre_split() - split a string using a regular expression

### SYNOPSIS

```lpc
string *pcre_split(string input, string pattern, int flags);
```

### DESCRIPTION

Returns the fields between successive non-overlapping matches of `pattern`.
The matched delimiters are removed. Empty fields are preserved, including
leading and trailing fields. If nothing matches, the result contains just
`input`.

The required `flags` argument accepts integer flags from `<pcre_flags.h>`,
such as `PCRE_I` (case-insensitive) and `PCRE_M` (multiline anchors). Pass `0`
for defaults. Unlike the other pattern-matching PCRE efuns, this efun does
not accept an options mapping. UTF-8 and Unicode character properties are
enabled by default; see [pcre_match](pcre_match).

Add `PCRE_SPLIT_DELIM_CAPTURE` to insert all parenthesized capture groups
after each field, in group-number order. Unmatched optional groups contribute
empty strings. Non-capturing groups do not contribute elements. To retain
the entire delimiter, enclose it in a capture group.

Zero-width matches also split the input. After an empty match, a non-empty
alternative at the same position is tried before advancing by one UTF-8 code
point (or a CRLF pair when the pattern's newline convention treats it as one).
Advancement does not discard input characters.

Invalid patterns, matching errors (including partial matches), and results
exceeding the configured maximum array size raise an error.

### EXAMPLES

```lpc
#include <pcre_flags.h>

pcre_split("one, two;three", "[,;]\\s*", 0);
// ({ "one", "two", "three" })

pcre_split(",a,,b,", ",", 0);
// ({ "", "a", "", "b", "" })

pcre_split("one,two;three", "([,;])", PCRE_SPLIT_DELIM_CAPTURE);
// ({ "one", ",", "two", ";", "three" })

pcre_split("aXb", "(x)", PCRE_I | PCRE_SPLIT_DELIM_CAPTURE);
// ({ "a", "X", "b" })
```

### SEE ALSO

[pcre_match](pcre_match), [pcre_extract](pcre_extract),
[pcre_match_all](pcre_match_all), [explode](../strings/explode)
