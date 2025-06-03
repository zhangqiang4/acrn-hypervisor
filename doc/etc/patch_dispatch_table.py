
import re
import sys

def patch_and_split_fulllineitems_block(tex_text, label):
    label_pattern = re.escape(label)
    start_pattern = (
        r"(\\begin\{fulllineitems\}\s*\n\s*"
        r"\\phantomsection\s*\\label\{\\detokenize\{" + label_pattern + r"\}\})"
    )

    match = re.search(start_pattern, tex_text)
    if not match:
        return tex_text
    # else:
    #     print(f"Label '{label}' is found in '{tex_file}'.")

    start_pos = match.start()
    end_pattern = r"\\end\{fulllineitems\}"
    end_match = re.search(end_pattern, tex_text[match.end():])
    if not end_match:
        print("No matching \\end{fulllineitems} found.")
        return tex_text

    block_start = start_pos
    block_end = match.end() + end_match.end()
    block = tex_text[block_start:block_end]

    # Find the \pysigstopmultiline line inside the block
    psml_match = re.search(r"(\\pysigstopmultiline)", block)
    if not psml_match:
        print("No \\pysigstopmultiline found in block.")
        return tex_text

    psml_start = psml_match.start()

    # Compose the modified block
    before_psml = block[:psml_start]
    after_psml = block[psml_start:]

    new_block = (
        "{\\fontsize{8}{9}\\selectfont\n"
        + before_psml +
        "}\n"
        + "{\\small\n"
        + after_psml +
        "\n}"
    )

    return tex_text[:block_start] + new_block + tex_text[block_end:]

if __name__ == "__main__":
    tex_file = sys.argv[1]
    with open(tex_file, "r", encoding="utf-8") as f:
        content = f.read()

    label = "developer-guides/mds/vp-base/vp-base_hv-main:c.dispatch_table"
    patched = patch_and_split_fulllineitems_block(content, label)

    with open(tex_file, "w", encoding="utf-8") as f:
        f.write(patched)
