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
TAB_SIZE = 2

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
    'public-attrib',  # should be added or no contents of structs/unions will be shown
]
