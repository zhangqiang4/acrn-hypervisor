import os
import sys

from typing import Any, Callable, cast, Dict, List, Optional, Type, Union

from docutils import nodes
from docutils.nodes import Node, TextElement

from sphinx import addnodes
from breathe.renderer import RenderContext
from breathe.renderer.sphinxrenderer import SphinxRenderer, \
    intersperse, DomainDirectiveFactory, BaseObject, NodeFinder, \
    WithContext, InlineText, get_param_decl, \
    get_definition_without_template_args

Declarator = Union[addnodes.desc_signature, addnodes.desc_signature_line]

need_inserted_path = os.path.dirname(os.path.abspath(__file__))
# if need_inserted_path not in sys.path
sys.path.insert(0, need_inserted_path)

BaseRender = SphinxRenderer


class BreatheRenderer(BaseRender):
    sections = BaseRender.sections

    # if need change Defines to Macros
    define_pos = 0
    for i, sect in enumerate(sections):
        if sect[0] == 'define':
            define_pos = i
    sections.pop(define_pos)
    sections.append(('define', 'Macros'))

    # add section title for structs
    sections.append(('struct', 'Data Structures'))

    # bind section_titles to class SphinxRenderer
    section_titles = dict(sections)
    BaseRender.section_titles = section_titles

    def visit_compounddef(self, node) -> List[Node]:  # add title for structs
        self.context = cast(RenderContext, self.context)
        options = self.context.directive_args[2]
        section_order = None
        if "sections" in options:
            section_order = {sec: i for i, sec in enumerate(options["sections"].split(" "))}
        membergroup_order = None
        if "membergroups" in options:
            membergroup_order = {sec: i for i, sec in enumerate(options["membergroups"].split(" "))}
        nodemap: Dict[int, List[Node]] = {}

        def addnode(kind, lam):
            if section_order is None:
                nodemap[len(nodemap)] = lam()  # innerclass run here
            elif kind in section_order:
                nodemap.setdefault(section_order[kind], []).extend(lam())

        if "members-only" not in options:
            if "allow-dot-graphs" in options:
                addnode("incdepgraph", lambda: self.render_optional(node.get_incdepgraph()))
                addnode("invincdepgraph", lambda: self.render_optional(node.get_invincdepgraph()))
                addnode(
                    "inheritancegraph", lambda: self.render_optional(node.get_inheritancegraph())
                )
                addnode(
                    "collaborationgraph",
                    lambda: self.render_optional(node.get_collaborationgraph()),
                )

            addnode("briefdescription", lambda: self.render_optional(node.briefdescription))
            addnode("detaileddescription", lambda: self.detaileddescription(node))

            def render_derivedcompoundref(node):
                if node is None:
                    return []
                output = self.render_iterable(node)
                if not output:
                    return []
                return [
                    nodes.paragraph(
                        "", "", nodes.Text("Subclassed by "), *intersperse(output, nodes.Text(", "))
                    )
                ]

            addnode(
                "derivedcompoundref", lambda: render_derivedcompoundref(node.derivedcompoundref)
            )

        section_nodelists: Dict[str, List[Node]] = {}
        # Get all sub sections
        for sectiondef in node.sectiondef:
            kind = sectiondef.kind
            if section_order is not None and kind not in section_order:
                continue
            header = sectiondef.header
            if membergroup_order is not None and header not in membergroup_order:
                continue
            child_nodes = self.render(sectiondef)  # sectiondef's nodes,i.e., macros
            if not child_nodes:
                # Skip empty section
                continue
            rst_node = nodes.container(classes=["breathe-sectiondef"])  # create a node
            rst_node.document = self.state.document  # get DOM object
            rst_node["objtype"] = kind  # node's objtype field
            rst_node.extend(child_nodes)  # add children to empty box, prepare the data
            # We store the nodes as a list against the kind in a dictionary as the kind can be
            # 'user-edited' and that can repeat so this allows us to collect all the 'user-edited'
            # entries together
            section_nodelists.setdefault(kind, []).append(rst_node)  # [{section_type: section_items}]

        # Order the results in an appropriate manner
        for kind, _ in self.sections:  # predefined sections: macros, enums, functions etc
            addnode(kind, lambda: section_nodelists.get(kind, []))  # order prefined sections according natural ordering

        # add class title
        if node.kind == "file" and node.innerclass:
            innerclass_cat = set()
            for cnode in node.innerclass:
                file_data = self.compound_parser.parse(cnode.refid)
                kind_ = file_data.compounddef.kind
                innerclass_cat.add(kind_)
            for kind in innerclass_cat:
                text = self.section_titles[kind]
                idtext = text.replace(" ", "-").lower()
                rubric = nodes.rubric(
                    text=text,
                    classes=["breathe-sectiondef-title"],
                    ids=["breathe-section-title-" + idtext],
                )
                section_nodelists.setdefault(kind, []).append(rubric)
                addnode(kind, lambda: section_nodelists.get(kind, []))

        # Take care of innerclasses
        addnode("innerclass", lambda: self.render_iterable(node.innerclass))
        addnode("innernamespace", lambda: self.render_iterable(node.innernamespace))

        if "inner" in options:
            for node in node.innergroup:
                file_data = self.compound_parser.parse(node.refid)  # enter inner component
                inner = file_data.compounddef
                addnode("innergroup", lambda: self.visit_compounddef(inner))

        nodelist = []
        for _, nodes_ in sorted(nodemap.items()):
            nodelist += nodes_

        return nodelist

    def visit_define(self, node) -> List[Node]:  # add spaces to seperate macro name and value
        declaration = node.name
        if node.param:
            declaration += "("
            for i, parameter in enumerate(node.param):
                if i:
                    declaration += ", "
                declaration += parameter.defname
            declaration += ")"

        # TODO: remove this once Sphinx supports definitions for macros
        def add_definition(declarator: Declarator) -> None:
            if node.initializer and self.app.config.breathe_show_define_initializer:
                declarator.append(nodes.Text(" \t\t"))  # add spaces to seperate macro name and value
                declarator.extend(self.render(node.initializer))

        return self.handle_declaration(node, declaration, declarator_callback=add_definition)

    # some methods should be modified and not be contained in methods
    met = {
        "visit_define": visit_define
    }

    for k, v in met.items():
        if k in SphinxRenderer.__dict__:
            setattr(BaseRender, k, v)

    # some methods should be modified and be contained in methods
    BaseRender.methods.update({
        "compounddef": visit_compounddef
    })


SphinxRenderer = BaseRender
