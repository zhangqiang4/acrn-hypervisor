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

from settings import UPDATED_SECTION_NAMES, SKIPPED_ITEMS,TAB, TAB_SIZE

Declarator = Union[addnodes.desc_signature, addnodes.desc_signature_line]

need_inserted_path = os.path.dirname(os.path.abspath(__file__))
# if need_inserted_path not in sys.path
sys.path.insert(0, need_inserted_path)

BaseRender = SphinxRenderer


class BreatheRenderer(BaseRender):
    sections = BaseRender.sections

    # update sections according settings.py
    section_titles = dict(sections)
    section_titles.update(dict(UPDATED_SECTION_NAMES))

    # bind section_titles to class SphinxRenderer
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
        def add_innerclass():
            if node.kind == 'file' and node.innerclass:
                res_set = set()
                for cnode in node.innerclass:
                    file_data = self.compound_parser.parse(cnode.refid)
                    kind_ = file_data.compounddef.kind
                    res_set.add(kind_)

                for kind in res_set:
                    # rubric should be first element of children in the container
                    text = self.section_titles[kind]
                    idtext = text.replace(" ", "-").lower()
                    rubric = nodes.rubric(
                        text=text,
                        classes=["breathe-sectiondef-title"],
                        ids=["breathe-section-title-" + idtext],
                    )

                    # retrieve children
                    child_nodes = []
                    top_level_node_names = set()
                    for cnode in node.innerclass:
                        cnode_path = cnode.valueOf_.split('.')
                        top_level_node_name = cnode_path[0]
                        top_level_node_names.add(top_level_node_name)
                        ref_node = self.compound_parser.parse(cnode.refid)
                        ref_node_kind = ref_node.compounddef.kind
                        if ref_node_kind == kind:
                            if len(cnode_path) != 1:  # skip if not top level nodes
                                continue
                            else:  # top_level_nodes
                                top_level_node = cnode
                                child_node = self.render(top_level_node)
                                child_nodes.extend(child_node)

                    if not child_nodes:
                        continue

                    # node installation
                    rst_node = nodes.container(classes=["breathe-sectiondef"])
                    rst_node.document = self.state.document
                    rst_node["objtype"] = kind
                    rst_node.append(rubric)
                    rst_node.extend(child_nodes)
                    section_nodelists.setdefault(kind, []).append(rst_node)
                    # render the node
                    addnode(kind, lambda: section_nodelists.get(kind, []))

        add_innerclass()

        # Take care of innerclasses
        # addnode("innerclass", lambda: self.render_iterable(node.innerclass))
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
                declarator.append(nodes.Text(f" {TAB*TAB_SIZE}"))  # add spaces to seperate macro name and value
                declarator.extend(self.render(node.initializer))

        return self.handle_declaration(node, declaration, declarator_callback=add_definition)

    def visit_sectiondef(self, node) -> List[Node]:
        self.context = cast(RenderContext, self.context)
        options = self.context.directive_args[2]
        node_list = []
        node_list.extend(self.render_optional(node.description))

        # Get all the memberdef info
        if "sort" in options:
            member_def = sorted(node.memberdef, key=lambda x: x.name)
        else:
            member_def = node.memberdef

        node_list.extend(self.render_iterable(member_def))

        if node_list:
            if "members-only" in options:
                return node_list

            if node.kind in SKIPPED_ITEMS:
                return node_list

            text = self.section_titles[node.kind]
            # Override default name for user-defined sections. Use "Unnamed
            # Group" if the user didn't name the section
            # This is different to Doxygen which will track the groups and name
            # them Group1, Group2, Group3, etc.
            if node.kind == "user-defined":
                if node.header:
                    text = node.header
                else:
                    text = "Unnamed Group"

            # Use rubric for the title because, unlike the docutils element "section",
            # it doesn't interfere with the document structure.
            idtext = text.replace(" ", "-").lower()
            rubric = nodes.rubric(
                text=text,
                classes=["breathe-sectiondef-title"],
                ids=["breathe-section-title-" + idtext],
            )
            res: List[Node] = [rubric]
            return res + node_list
        return []

    # some methods should be modified and not be contained in methods
    met = {
        "visit_define": visit_define
    }

    for k, v in met.items():
        if k in SphinxRenderer.__dict__:
            setattr(BaseRender, k, v)

    # some methods should be modified and be contained in methods
    BaseRender.methods.update({
        "compounddef": visit_compounddef,
        "sectiondef": visit_sectiondef
    })


SphinxRenderer = BaseRender
