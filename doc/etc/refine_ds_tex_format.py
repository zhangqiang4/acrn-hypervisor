import re
import sys

def reduce_fontsize_and_setstretch(tex_text, label_suffix, new_fontsize, new_stretch):
    pattern = re.compile(
        r"(\\begin\{fulllineitems\}\s*\n\s*"
        r"\\phantomsection\s*\\label\{\\detokenize\{([^\}]+?" + re.escape(label_suffix) + r")\}\})"
        r"(.*?)"
        r"(\\pysigstopmultiline.*?)?\\end\{fulllineitems\}",
        re.DOTALL
    )

    def shrink_block(match):
        full_start = match.group(1)
        block_content = match.group(3)
        rest = match.group(4) or ""
        org_fontsize = 10

        return (
            f"{{\\setstretch{{{new_stretch}}}\n"
            f"{{\\fontsize{{{new_fontsize}}}{{{int(new_fontsize)+1}}}\\selectfont\n"
            f"{full_start}{block_content}}}\n"
            f"}}\n"
            f"{{\\fontsize{{{org_fontsize}}}{{{int(org_fontsize)+1}}}\\selectfont\n"
            f"{rest}"
            f"\\end{{fulllineitems}}\n}}"
        )

    return pattern.sub(shrink_block, tex_text)


def process_pysigline(line, keep_nth=2):
    # Full formatting pattern to match:
    pattern = (
        r'(\\DUrole\{w\}\{\s*\}\\DUrole\{p\}\{\\newline \}'
        r'\\DUrole\{p\}\{ \}\\DUrole\{w\}\{\s*\}\\DUrole\{p\}\{\\hspace\*{2em} \})'
    )

    parts = re.split(pattern, line)
    result = []
    match_count = 0

    for part in parts:
        if re.fullmatch(pattern, part):
            match_count += 1
            if match_count % keep_nth == 0:
                result.append(part)  # Keep the Nth match
        else:
            result.append(part)

    return ''.join(result)


def patch_pysigline_block(tex_text, label_suffix, keep_nth=2):
    pattern = re.compile(
        r"(\\begin\{fulllineitems\}\s*\n\s*"
        r"\\phantomsection\s*\\label\{\\detokenize\{([^\}]+?" + re.escape(label_suffix) + r")\}\})"
        r"(.*?)\\end\{fulllineitems\}",
        re.DOTALL
    )

    def modify_block(match):
        header = match.group(1)
        content = match.group(3)
        psml_match = re.search(r"(\\pysigstopmultiline)", content)
        if not psml_match:
            return match.group(0)

        psml_start = psml_match.start()
        before_psml = content[:psml_start]
        after_psml = content[psml_start:]

        lines = before_psml.splitlines(keepends=True)
        modified = []
        for line in lines:
            if line.lstrip().startswith(r'\pysigline'):
                modified.append(process_pysigline(line, keep_nth))
            else:
                modified.append(line)

        return header + ''.join(modified) + after_psml + r"\end{fulllineitems}"

    return pattern.sub(modify_block, tex_text)

def tweak_newline(tex_text):
    # Pattern 1: Remove ~\newline after \end{fulllineitems} or \end{itemize}, along with trailing blank lines
    tex_text = re.sub(
        r'(\\end\{(?:fulllineitems|itemize)\})'  # group 1: block end
        r'(?:\s*\n)*'                            # optional blank lines
        r'\s*~\\newline\s*\n?',                  # the ~\newline line
        r'\1\n',
        tex_text,
        flags=re.MULTILINE
    )

    # Pattern 2: Remove only the ~\newline after \begin{sphinxadmonition}{note}{Remark},
    #            preserve any blank lines before it
    tex_text = re.sub(
        r'(?<=\\begin\{sphinxadmonition\}\{note\}\{Remark\}\n\n)'  # lookbehind
        r'\s*~\\newline\s*\n?',                                    # remove ~\newline only
        '',
        tex_text,
        flags=re.MULTILINE
    )

    return tex_text

if __name__ == "__main__":
    tex_file = sys.argv[1]
    with open(tex_file, "r", encoding="utf-8") as f:
        content = f.read()

    # Adjust fontsize and spacing in dispatch_table[]
    new_content = reduce_fontsize_and_setstretch(
        content,
        label_suffix="c.dispatch_table",
        new_fontsize="8",
        new_stretch="1.1"
    )

    # Clean up long repetitive formatting lines in unsupported_msrs[].
    # To save up the space and for better layout, only keep the 2nd occurrences of
    # \DUrole{w}{   }\DUrole{p}{\newline }\DUrole{p}{ }\DUrole{w}{   }\DUrole{p}{\hspace*{2em} }
    new_content = patch_pysigline_block(
        new_content,
        label_suffix="c.unsupported_msrs",
        keep_nth=2
    )

    # Adjust fontsize and spacing in unsupported_msrs[]
    new_content = reduce_fontsize_and_setstretch(
        new_content,
        label_suffix="c.unsupported_msrs",
        new_fontsize="10",
        new_stretch="1.2"
    )

    # Remove unnecessary ~\newline
    new_content = tweak_newline(new_content)

    with open(tex_file, "w", encoding="utf-8") as f:
        f.write(new_content)
