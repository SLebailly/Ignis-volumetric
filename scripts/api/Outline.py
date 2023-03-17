#!/usr/bin/env python3
# coding: utf-8

from utils import load_api
import inspect
import re

SUBSECTION = "-----------------------------------------------"
SUBSUBSECTION = "^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^"


def dir_class(c):
    for prop_name in dir(c):
        if prop_name.startswith("__"):
            continue

        try:
            data = getattr(c, prop_name)
        except:
            continue

        yield prop_name, data


def cleanup_doc(doc):
    return doc.replace("ignis.pyignis.", "").strip()


single_newline = re.compile(r"([^\n])\n([^\n])")
func_match = re.compile(r"\w+\([^\n]*\)([\t ]*->[\t ]*[\[\]\.{} \t\,\w]+)?")
ident_line = re.compile(r"\n([^\n])")
list_with_content = re.compile(r"(\d+\.[^\n]+)\n\n([^\d].*)")
# Documentation generated by pybind11
enum_entry_section = re.compile(r"^\s{2}(\w+)$", re.M)


def prepare_lines(lines):
    '''Prepare lines such that single new lines are collapsed'''
    return re.sub(single_newline, r"\g<1> \g<2>", lines)


def ident_lines(lines, prefix=" "):
    return re.sub(ident_line, r"\n"+prefix+r"\g<1>", lines)


def handle_single_func(line, identifiers):
    for ident in identifiers:
        line = re.sub(r"\b(?<!{)"+re.escape(ident)+r"(?!})", r"{"+re.escape(ident)+r"}", line)
    return line


def escape_func(lines, identifiers):
    lines = prepare_lines(lines)
    lines = re.sub(func_match, lambda match: handle_single_func(
        match.group(0), identifiers), lines)
    return re.sub(func_match, r":pythonfunc:`\g<0>`", lines)


def handle_func_doc(lines, identifiers):
    lines = prepare_lines(lines)

    func_signature = re.match(func_match, lines).group(0)
    documentation = escape_func(lines.replace(
        func_signature, "").strip(), identifiers)
    documentation = re.sub(
        list_with_content, r"\g<1>\n\n   \g<2>", documentation)
    documentation = ident_lines(documentation, "  ")

    func_line = f"- :pythonfunc:`{handle_single_func(func_signature, identifiers)}`"

    doc_line = f"  {documentation}"

    return f"{func_line}\n\n{doc_line}"


def handle_class_doc(lines):
    if lines is None:
        return "- :pythonfunc:`Missing Documentation`\n"
    else:
        return re.sub(enum_entry_section, r"- :pythonfunc:`\g<1>`\n", lines).replace("Members:", f"Entries\n{SUBSUBSECTION}\n")


def get_class_names(root):
    classes = []
    todo = [("", root)]
    while len(todo) > 0:
        name, cur = todo.pop(0)
        for prop_name, data in dir_class(cur):
            if inspect.isclass(data):
                if cur != root:
                    classes.append(f"{name}.{prop_name}")
                    todo.append((f"{name}.{prop_name}", data))
                else:
                    classes.append(prop_name)
                    todo.append((prop_name, data))

    return classes


def generate_doc(root):
    identifiers = get_class_names(ignis)
    identifiers.sort(key=len, reverse=True)

    doc_str = ""
    todo = [("Ignis (module)", root)]
    while len(todo) > 0:
        name, cur = todo.pop(0)

        doc_str += f'.. _{name}:\n\n{name}\n{SUBSECTION}\n\n{handle_class_doc(inspect.getdoc(cur))}\n\n'

        # Properties
        has_props = False
        for prop_name, data in dir_class(cur):
            if isinstance(data, property):
                inner_doc = cleanup_doc(inspect.getdoc(data))

                if not has_props:
                    doc_str += f"Properties\n{SUBSUBSECTION}\n\n"
                    has_props = True
                doc_str += f'- :pythonfunc:`{prop_name}`: {escape_func(inner_doc, identifiers)}\n\n'

        # Methods
        has_funcs = False
        for prop_name, data in dir_class(cur):
            if inspect.isroutine(data):
                inner_doc = cleanup_doc(inspect.getdoc(data))
                if inner_doc == "":
                    continue

                if not has_funcs:
                    doc_str += f"Methods\n{SUBSUBSECTION}\n\n"
                    has_funcs = True
                doc_str += f'{handle_func_doc(inner_doc, identifiers)}\n\n'

        # Embedded classes
        for prop_name, data in dir_class(cur):
            if inspect.isclass(data):
                if cur != root:
                    todo.append((f"{name}.{prop_name}", data))
                else:
                    todo.append((prop_name, data))

        doc_str += "\n"

    return doc_str.rstrip()


if __name__ == "__main__":
    ignis = load_api()
    print(generate_doc(ignis))
