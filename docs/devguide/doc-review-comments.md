# Review comments

**You can leave notes on a document for Claude to act on, without those notes ever touching the
tree.** Mark up a *copy* of a file, ask Claude to address the comments, and the master `.md` in
the repository is revised in place. The copy is scratch; the master never carries a marker, so
there is nothing to strip out afterward and nothing that can leak into a build.

The mechanism is an HTML comment with a sentinel. HTML comments are invisible in rendered
Markdown and in the built PDFs, and they are trivial to `grep` for — the same trick the
generated `docs/manual/ref/*.md` banner and the `docs/boards/_TEMPLATE.md` author notes already
use.

## The marker

Any HTML comment that begins with `@claude`:

```markdown
Some sentence that reads awkwardly. <!-- @claude: too terse — expand this. -->
```

For a longer note, use the block form:

```markdown
<!-- @claude
This whole section assumes the reader already knows what a hard-sector disk is.
Add a one-paragraph primer before it.
-->
```

When several people are annotating one file, tag who wrote the note:

```markdown
<!-- @claude(patrick): reword this for a first-time user. -->
```

Place the marker next to the text it is about — the sentence it follows, or immediately above
the section it refers to. Claude anchors the change to where the marker sits, so position is how
you say *what* the comment is about.

## The workflow

1. **Copy** the file you want to review to somewhere outside the repository, keeping its
   filename. `docs/devguide/theory.md` becomes `~/Desktop/theory.md`, say. The basename is the
   link back to the master, so do not rename it.
2. **Annotate** the copy: drop `@claude` markers wherever you want a change.
3. **Ask** Claude to act on it — *"address the @claude comments in `~/Desktop/theory.md`"*.
   Claude reads the markers, matches the copy to the repo file of the same name, and revises the
   **master** to address each one.

To see every outstanding marker across the tree (they should only ever appear in copies, never
here):

```sh
grep -rn '<!-- @claude' .
```

## What Claude does with them

These are the rules that keep the process safe — worth knowing so you can predict the result:

- **It matches by filename.** The annotated copy shares the master's basename; Claude finds the
  repo file with that name and edits *that*. If more than one file in the tree has the name (a
  bare `README.md`, an `ORDER`), Claude asks which one rather than guessing.
- **The copy carries comments, not content.** Your copy may be hours or days out of date. Claude
  treats the markers — and the text immediately around each one — as the input, and applies them
  to the *current* master. It does not fold unrelated edits from the copy back into the tree, so
  a stale copy cannot silently revert someone else's work.
- **It works on a branch,** per the project rule that every change lands on a branch off
  `master`.
- **There is nothing to clean up.** The master never had a marker, so none is left behind.
  Claude reports what it changed, comment by comment, and leaves your annotated copy alone — it
  is yours.

## Annotating in place (the exception)

You *can* drop markers straight into a repo file instead of a copy — handy for a quick note you
will resolve in the same sitting. If you do, Claude removes each marker once it is addressed, so
none survives the commit.

One place this is not allowed: the **User Manual** (`docs/manual/*.md`). Its raw text —
comments included — is grep-checked by `tests/acceptance/docs-manual.cmake` for anything that
points outside the shipped package (`src/`, `tests/`, `ctest`, a header name, and so on). A
marker that mentions one of those tokens will fail the build even though it never renders. For
the manual, review a copy.
