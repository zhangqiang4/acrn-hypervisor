#!/usr/bin/python3

# you can change sections' names by updating tuple's second component relatively
UPDATED_SECTION_NAMES = [
    ('define', 'Macros'),
    ('struct', 'Data Structures'),
    ('union', 'Unions'),
    ("enum", "Enums"),
    ("typedef", "Typedefs"),
    ("func", "Functions"),
    ("var", "Global Variables"),
]

# to define items skipped by SKIPPED_ITEMS, which means the items will be hided from web page
SKIPPED_ITEMS = [
    "public-attrib"
]

# you can enlarge width between macro's name and its value by increase TAB_SIZE
TAB = '\t'
TAB_SIZE = 8
LINE_BREAKER = '\\'
CR = '\n'
SPACE = ' '

#  MAX_INDENTATION_NUMBERS tag determines the maximum number of indentations
#  that the depth of embedded components can have for it to appear in
#  the initializer.
MAX_INDENTATION_NUMBERS = 100

# if set to False will adopt original breathe's source code
NEED_DEFINED_SORTING = True

# sorting sections according to the list SECTIONS_SORTING_SEQUENCE
# which items are retrieved from the first component of selected tuples
# you can look up the tuples from UPDATED_SECTION_NAMES & breathe's global variable: sections
# ref: ./breathe/renderer/sphinxrenderer.py
SECTIONS_SORTING_SEQUENCE = [
    'var',
    'typedef',
    'define',
    'enum',
    'union',
    'struct',
    'func',
    'public-attrib',
    # should be added or no contents of structs/unions will be shown, please place after struct & union
]

# modify initializer's rendering style & make it closer to c code
PERIOD = ','
NL_REPL = '__NL__'
NEW_LINE = PERIOD + NL_REPL + PERIOD

# support intervals in initializers, i.e., [beg..end] = {1,2,...}
ESCAPE_SYMBOLS = {
    '[': '__LEFT_BRACKET__',
    ']': '__RIGHT_BRACKET__',
    '..': '__INTERVAL__',
    '(': '__LEFT_PARENTHESIS__',
    ')': '__RIGHT_PARENTHESIS__',
}

# redefine type alias
REDEFINED_TYPES = {
    'acrn_bool': 'bool',
}
