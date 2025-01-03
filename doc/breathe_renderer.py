import os
import sys

from sphinx.domains.c import (
    ASTIdentifier,
    ASTNestedName,
    Symbol,
    SymbolLookupResult,
    CDomain,
    DefinitionParser,
    LookupKey,
    logger,
    ASTAssignmentExpr,
    ASTExpression,
    _expression_assignment_ops
)

from breathe.renderer import RenderContext
from breathe.renderer.sphinxrenderer import SphinxRenderer, \
    intersperse, NodeFinder

from typing import (Callable, Dict, List, Optional, Tuple, Union, cast)

from docutils import nodes
from docutils.nodes import Element, Node, TextElement

from sphinx import addnodes
from sphinx.addnodes import pending_xref
from sphinx.builders import Builder
from sphinx.environment import BuildEnvironment

from sphinx.util.cfamily import (DefinitionError, verify_description_mode)

from settings import UPDATED_SECTION_NAMES, SKIPPED_ITEMS, TAB, TAB_SIZE

Declarator = Union[addnodes.desc_signature, addnodes.desc_signature_line]

need_inserted_path = os.path.dirname(os.path.abspath(__file__))
# if need_inserted_path not in sys.path
sys.path.insert(0, need_inserted_path)

from hookit import HookManager


class BreatheASTIdentifier:
    def followed_by_anon(self, identifiers):
        for i, ident in enumerate(identifiers):
            if (ident.identifier == self.identifier) and i + 2 <= len(identifiers):
                followed = identifiers[i + 1]
                return followed.is_anon()

    meth = {
        'followed_by_anon': followed_by_anon,
    }


class BreatheASTNestedName:
    def describe_signature(self, signode: TextElement, mode: str,
                           env: "BuildEnvironment", symbol: "Symbol") -> None:
        verify_description_mode(mode)
        # just print the name part, with template args, not template params
        if mode == 'noneIsName':
            if self.rooted:
                raise AssertionError("Can this happen?")  # TODO
                signode += nodes.Text('.')
            for i in range(len(self.names)):
                if i != 0:
                    raise AssertionError("Can this happen?")  # TODO
                    signode += nodes.Text('.')
                n = self.names[i]
                n.describe_signature(signode, mode, env, '', symbol)
        elif mode == 'param':
            assert not self.rooted, str(self)
            assert len(self.names) == 1
            self.names[0].describe_signature(signode, 'noneIsName', env, '', symbol)
        elif mode in ('markType', 'lastIsName', 'markName'):
            # Each element should be a pending xref targeting the complete
            # prefix.
            prefix = ''
            first = True
            names = self.names[:-1] if mode == 'lastIsName' else self.names
            # If lastIsName, then wrap all of the prefix in a desc_addname,
            # else append directly to signode.
            # TODO: also for C?
            #  NOTE: Breathe previously relied on the prefix being in the desc_addname node,
            #       so it can remove it in inner declarations.
            dest = signode
            if mode == 'lastIsName':
                dest = addnodes.desc_addname()
            if self.rooted:
                prefix += '.'
                if mode == 'lastIsName' and len(names) == 0:
                    # signode += addnodes.desc_sig_punctuation('.', '.')
                    signode += addnodes.desc_sig_punctuation('.', '')
                else:
                    # dest += addnodes.desc_sig_punctuation('.', '.')
                    dest += addnodes.desc_sig_punctuation('.', '')

            for i in range(len(names)):
                ident = names[i]
                if ident.followed_by_anon(names):
                    continue
                elif not ident.is_anon():
                    txt_ident = str(ident)
                    if txt_ident != '':
                        ident.describe_signature(dest, 'markType', env, prefix, symbol)
                    prefix += txt_ident
                if ident.is_anon() and first:
                    txt_ident = str(ident)
                    if txt_ident != '':
                        dest += addnodes.desc_sig_space()
                        ident.describe_signature(dest, 'markType', env, prefix, symbol)
                    prefix += txt_ident
                    first = False

            if mode == 'lastIsName':
                if len(self.names) > 1:
                    dest += addnodes.desc_sig_punctuation('.', '.')
                    dest += addnodes.desc_sig_punctuation('.', '')
                    signode += dest
                self.names[-1].describe_signature(signode, mode, env, '', symbol)
        else:
            raise Exception('Unknown description mode: %s' % mode)

    meth = {
        'describe_signature': describe_signature,
    }


class BreatheSymbol:
    def _symbol_lookup(self, nestedName: ASTNestedName,
                       onMissingQualifiedSymbol: Callable[["Symbol", ASTIdentifier], "Symbol"],  # NOQA
                       ancestorLookupType: str, matchSelf: bool,
                       recurseInAnon: bool, searchInSiblings: bool) -> SymbolLookupResult:
        # TODO: further simplification from C++ to C
        # ancestorLookupType: if not None, specifies the target type of the lookup
        if Symbol.debug_lookup:
            Symbol.debug_indent += 1
            Symbol.debug_print("_symbol_lookup:")
            Symbol.debug_indent += 1
            Symbol.debug_print("self:")
            print(self.to_string(Symbol.debug_indent + 1), end="")
            Symbol.debug_print("nestedName:        ", nestedName)
            Symbol.debug_print("ancestorLookupType:", ancestorLookupType)
            Symbol.debug_print("matchSelf:         ", matchSelf)
            Symbol.debug_print("recurseInAnon:     ", recurseInAnon)
            Symbol.debug_print("searchInSiblings:  ", searchInSiblings)

        names = nestedName.names
        # print('lookup:', [n.identifier for n in names])

        # find the right starting point for lookup
        parentSymbol = self
        if len(names) > 1:
            ...
            # symbols, ident=[Symbol(None,None,None,None,None)],names[-1]
            # return SymbolLookupResult(symbols, parentSymbol, ident)

        if nestedName.rooted:  # find starting point
            while parentSymbol.parent:
                parentSymbol = parentSymbol.parent
        if ancestorLookupType is not None:
            # walk up until we find the first identifier
            firstName = names[0]
            while parentSymbol.parent:
                if parentSymbol.find_identifier(firstName,
                                                matchSelf=matchSelf,
                                                recurseInAnon=recurseInAnon,
                                                searchInSiblings=searchInSiblings):
                    break
                parentSymbol = parentSymbol.parent

        if Symbol.debug_lookup:
            Symbol.debug_print("starting point:")
            print(parentSymbol.to_string(Symbol.debug_indent + 1), end="")

        # and now the actual lookup
        for ident in names[:-1]:
            symbol = parentSymbol._find_first_named_symbol(
                ident, matchSelf=matchSelf, recurseInAnon=recurseInAnon)
            if symbol is None:
                symbol = onMissingQualifiedSymbol(parentSymbol, ident)
                if symbol is None:
                    if Symbol.debug_lookup:
                        Symbol.debug_indent -= 2
                    return None
            # We have now matched part of a nested name, and need to match more
            # so even if we should matchSelf before, we definitely shouldn't
            # even more. (see also issue #2666)
            matchSelf = False
            parentSymbol = symbol

        if Symbol.debug_lookup:
            Symbol.debug_print("handle last name from:")
            print(parentSymbol.to_string(Symbol.debug_indent + 1), end="")

        # handle the last name
        ident = names[-1]

        symbols = parentSymbol._find_named_symbols(
            ident, matchSelf=matchSelf,
            recurseInAnon=recurseInAnon,
            searchInSiblings=searchInSiblings)

        if Symbol.debug_lookup:
            symbols = list(symbols)  # type: ignore
            Symbol.debug_indent -= 2
        symbols = []  # =================================================== redefinition
        return SymbolLookupResult(symbols, parentSymbol, ident)

    meth = {
        '_symbol_lookup': _symbol_lookup,
    }


class BreatheCDomain:
    def _resolve_xref_inner(self, env: BuildEnvironment, fromdocname: str, builder: Builder,
                            typ: str, target: str, node: pending_xref,
                            contnode: Element) -> Tuple[Optional[Element], Optional[str]]:
        parser = DefinitionParser(target, location=node, config=env.config)
        try:
            name = parser.parse_xref_object()
        except DefinitionError as e:
            logger.warning('Unparseable C cross-reference: %r\n%s', target, e,
                           location=node)
            return None, None
        parentKey: LookupKey = node.get("c:parent_key", None)
        rootSymbol = self.data['root_symbol']
        if parentKey:
            parentSymbol: Symbol = rootSymbol.direct_lookup(parentKey)
            if not parentSymbol:
                print("Target: ", target)
                print("ParentKey: ", parentKey)
                print(rootSymbol.dump(1))
            assert parentSymbol  # should be there
        else:
            parentSymbol = rootSymbol
        s = parentSymbol.find_declaration(name, typ,
                                          matchSelf=True, recurseInAnon=True)
        if s is None or s.declaration is None:
            return None, None

        # TODO: check role type vs. object type

        declaration = s.declaration
        displayName = name.get_display_string()
        docname = s.docname
        assert docname

        return ['']

    def resolve_xref(self, env: BuildEnvironment, fromdocname: str, builder: Builder,
                     typ: str, target: str, node: pending_xref,
                     contnode: Element) -> Optional[Element]:
        return []

    meth = {
        '_resolve_xref_inner': _resolve_xref_inner,
        'resolve_xref': resolve_xref
    }


class BreatheDefinitionParser:
    """
    Parse initializers as follows:
    1. Sphinx has supported: static struct X x = {1, 2};
    2. Sphinx has supported: static struct X x = {x = 1, y = 2};
    3. Newly added: static struct X x = {x = {1, 2}};
    4. Newly added: static struct X x = {x = {y = 1}};
    5. Newly added: static struct X x = {{1, 2}, {3, 4}};
    6. Newly added: static struct X x = {{x1 = 1, x2 = 2}, {y1 = 3, y2 = 4}};
    7. Miscellaneous: static struct X x =
    {1, x1 = 2, x2 = {{3}, 4}, x3 = {y1 = 5, y2 = {z1 = 6}}, {7, 8, {9}}, {x4 = 10, x5 = 11}};
    """
    def _parse_inner_initializer_or_expression(self):
        # Parse inner initializer
        # eg1. static struct X x = {{1,2},{3,4}};
        # eg2. static struct X x = {{x1 = 1, x2 = 2},{y1 = 3, y2 = 4}};
        self.skip_ws()
        if self.definition[self.pos] not in '{':  # inner initializer
            return self._parse_expression()
        else:
            return self._parse_braced_init_list()

    def _parse_initializer_list(self, name: str, open: str, close: str
                                ) -> Tuple[List[ASTExpression], bool]:
        # Parse open and close with the actual initializer-list in between
        # -> initializer-clause '...'[opt]
        #  | initializer-list ',' initializer-clause '...'[opt]
        # TODO: designators
        self.skip_ws()
        if not self.skip_string_and_ws(open):
            return None, None
        if self.skip_string(close):
            return [], False

        exprs = []
        trailingComma = False
        while True:
            self.skip_ws()
            expr = self._parse_inner_initializer_or_expression()  # initializer or expression, make a judgement!
            self.skip_ws()
            exprs.append(expr)
            self.skip_ws()
            if self.skip_string(close):
                break
            if not self.skip_string_and_ws(','):
                self.fail("Error in %s, expected ',' or '%s'." % (name, close))
            if self.current_char == close and close == '}':
                self.pos += 1
                trailingComma = True
                break
        return exprs, trailingComma

    def _parse_initializer_or_expression(self):
        # Parse initializer
        # eg3. static struct X x = {x1 = {1, 2}, x2 = {3, 4}};
        # eg4. static struct X x = {x1 = {x2 = 1, x3 = 2}, y1 = {y2 = 3, y3 = 4}};
        self.skip_ws()
        if self.definition[self.pos] in '{':  # initializer
            return self._parse_braced_init_list()
        else:
            return self._parse_logical_or_expression()

    def _parse_assignment_expression(self) -> ASTExpression:
        # -> conditional-expression
        #  | logical-or-expression assignment-operator initializer-clause
        # -> conditional-expression ->
        #     logical-or-expression
        #   | logical-or-expression "?" expression ":" assignment-expression
        #   | logical-or-expression assignment-operator initializer-clause
        exprs = []
        ops = []
        orExpr = self._parse_logical_or_expression()
        exprs.append(orExpr)  # lvals
        # TODO: handle ternary with _parse_conditional_expression_tail
        while True:
            oneMore = False
            self.skip_ws()
            for op in _expression_assignment_ops:
                if op[0] in 'abcnox':
                    if not self.skip_word(op):
                        continue
                else:
                    if not self.skip_string(op):
                        continue
                # parse rvals
                expr = self._parse_initializer_or_expression()  # initializer or expression, make a judgement!
                exprs.append(expr)  # rvals
                ops.append(op)  # ops
                oneMore = True
            if not oneMore:
                break
        return ASTAssignmentExpr(exprs, ops)  # lval = rval

    meth = {
        '_parse_inner_initializer_or_expression': _parse_inner_initializer_or_expression,
        '_parse_initializer_list': _parse_initializer_list,
        '_parse_initializer_or_expression': _parse_initializer_or_expression,
        '_parse_assignment_expression': _parse_assignment_expression
    }


class BreatheRenderer:
    sections = SphinxRenderer.sections

    # update sections according settings.py
    section_titles = dict(sections)
    section_titles.update(dict(UPDATED_SECTION_NAMES))

    # bind section_titles to class SphinxRenderer
    SphinxRenderer.section_titles = section_titles

    def visit_compounddef(self, node) -> List[Node]:
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

        def add_innerclass():
            if node.kind == 'file' and node.innerclass:
                res_set = set()
                for cnode in node.innerclass:
                    file_data = self.compound_parser.parse(cnode.refid)
                    kind_ = file_data.compounddef.kind
                    res_set.add(kind_)

                top_level_node_names = set()
                inner_level_nodes = {}
                for cnode in node.innerclass:
                    top_level_node_names.add(cnode.valueOf_.split('.')[0])
                    ref_node = self.compound_parser.parse(cnode.refid)

                    ref_node_kind = ref_node.compounddef.kind
                    if ref_node_kind in ['struct', 'union']:
                        cnode_path = cnode.valueOf_.split('.')
                        if len(cnode_path) > 1:
                            inner_level_nodes.update({'.'.join(cnode_path): ref_node.compounddef})

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

                    inner_level_names = list(inner_level_nodes.keys())
                    all_level_names = list(top_level_node_names) + inner_level_names
                    path_depth = dict(map(lambda x: (x, len(x.split('.'))), all_level_names))

                    total_path = list(path_depth.keys())

                    # store mapping (path -> node) to path_node_map
                    path_node_map = {}
                    id_map = {cn.valueOf_: cn.refid for cn in node.innerclass}
                    for cn in node.innerclass:
                        cn_path = cn.valueOf_
                        ref_id = id_map[cn_path.split('.', maxsplit=1)[0]]
                        file_data_ = self.compound_parser.parse(ref_id)
                        parent_kind = file_data_.compounddef.kind
                        path_node_map.update({cn_path: (parent_kind, cn)})

                    def fetch_node(path):
                        file_data = path_node_map[path][1]
                        return self.render(file_data)

                    class Path:
                        def __init__(self, p=''):
                            self.p = p
                            self.kind = ''
                            self.visited = False

                        @property
                        def children(self):
                            if not self.p:
                                return [path_map[p] for p in total_path if len(p.split('.')) == 1]
                            return [path_map[p] for p in total_path if
                                    p.startswith(self.p) and len(p.split('.')) == len(self.p.split('.')) + 1]

                    p_ = Path()
                    path_map = {p: Path(p) for p in list(path_node_map.keys())}

                    def render_by_path(node, path=p_):
                        """
                        Generally speaking, it's no need to place a recursive function
                        if you implement DFS algorithm by stack.

                        But I observed that the structs or unions will be placed line by line,
                        it doesn't meet with the requirement: CHILDREN SHOULD BE INSERTED INTO THEIR PARENT.

                        For example, one field in one struct will be rendered to HTML by one <dl> element,
                        with type, name and value of the field respectively placed in one <dt> element,
                        separated by several <span> elements of course.
                        If that field is one embedded anonymous struct,
                        it should be placed in its parent's <dd> element and
                        otherwise the <dd> element will be empty by default.

                        So I use one stack and recursive function to fulfill the requirement.
                        """
                        child_nodes = []

                        if not path.p:
                            path_ = p_.children
                        else:
                            path_ = [path]

                        while len(path_) > 0:
                            # it will run twice if there are structs and unions
                            if path_node_map[path_[0].p][0] != kind:
                                path_.pop(0)
                                continue
                            p = path_.pop(0)
                            p.visited = True
                            child_node = fetch_node(p.p)

                            # find parents(i.e. fields) which type is struct/union and
                            # push its children into its empty <dd> elements
                            node_ = child_node[1]
                            finder = NodeFinder(node_.document)
                            node_.walk(finder)
                            contentnode = finder.content

                            def branch(contentnode, branch_no):
                                descs = contentnode.children[branch_no].children
                                for i, dc in enumerate(descs):  # <desc>=[dt,dd]
                                    if i % 2 == 1:
                                        dt_id = dc.children[0].attributes['ids']
                                        if not dt_id:
                                            continue
                                        desc_sig_id = dt_id[0].split('.', maxsplit=1)[1]  # drop domain
                                        desc_content = dc.children[1]
                                        if desc_sig_id not in inner_level_nodes:
                                            continue
                                        nn = inner_level_nodes[desc_sig_id]
                                        if nn and not desc_content.children:
                                            p = path_map[desc_sig_id]
                                            if not p.visited:
                                                inner_result = render_by_path(nn, path=path_map[desc_sig_id])
                                                node_ = inner_result[1]
                                                finder = NodeFinder(node_.document)
                                                node_.walk(finder)
                                                contentnode_inner = finder.content
                                                desc_content.extend(contentnode_inner)

                            # first time, you will reach the branch started by "#include xxx"
                            # later on, you will reach the branch which omit header "#include xxx"
                            br_no = 0 if len(contentnode.children) == 1 else 1
                            branch(contentnode, br_no)

                            child_nodes.extend(child_node)

                        return child_nodes

                    current_level_nodes = render_by_path(node)
                    child_nodes.extend(current_level_nodes)

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
        addnode("innernamespace", lambda: self.render_iterable(node.innernamespace))

        if "inner" in options:
            for node in node.innergroup:
                file_data = self.compound_parser.parse(node.refid)  # enter inner component
                inner = file_data.compounddef
                addnode("innergroup", lambda: self.visit_compounddef_(inner))

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
                declarator.append(nodes.Text(f" {TAB * TAB_SIZE}"))  # add spaces to seperate macro name and value
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

    # some methods should be modified and not be contained in `methods`
    meth = {
        "visit_define": visit_define
    }

    # some methods should be modified and be contained in `methods`
    SphinxRenderer.methods.update({
        "compounddef": visit_compounddef,
        "sectiondef": visit_sectiondef
    })


def setup():
    HookManager(ASTIdentifier).hook(mapping=BreatheASTIdentifier.meth)
    HookManager(ASTNestedName).hook(mapping=BreatheASTNestedName.meth)
    HookManager(Symbol).hook(mapping=BreatheSymbol.meth)
    HookManager(CDomain).hook(mapping=BreatheCDomain.meth)
    HookManager(DefinitionParser).hook(mapping=BreatheDefinitionParser.meth)
    HookManager(SphinxRenderer).hook(mapping=BreatheRenderer.meth)
