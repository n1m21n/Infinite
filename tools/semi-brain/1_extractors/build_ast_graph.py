#!/usr/bin/env python3
"""
build_ast_graph.py
Extracts the complete C++ AST call-graph and symbol index from src/ using Tree-sitter:
- Classes, structs, namespaces, and types
- Function & method definitions with line numbers
- Caller -> Callee forward call edges
- Callee -> Callers reverse call graph (Blast Radius calculation)
- Include dependency graph
- Subsystem clustering
"""

import json
import os
import re
from pathlib import Path
from collections import defaultdict

import tree_sitter_cpp as tscpp
from tree_sitter import Language, Parser

REPO_PATH = Path(__file__).resolve().parents[3]
SRC_DIR = REPO_PATH / "src"
OUTPUT_DIR = Path(__file__).resolve().parent / "output"
OUTPUT_FILE = OUTPUT_DIR / "ast_symbol_graph.json"

CPP_LANGUAGE = Language(tscpp.language())
parser = Parser(CPP_LANGUAGE)

def get_node_text(node, source_bytes):
    return source_bytes[node.start_byte:node.end_byte].decode("utf-8", errors="replace")

def infer_subsystem(file_path: str) -> str:
    fp = file_path.lower()
    if "src/audio/" in fp or "src/dsp/" in fp:
        return "audio_dsp"
    elif "src/arrange/" in fp:
        return "arrange_timeline"
    elif "src/nodes/" in fp:
        return "nodes"
    elif "src/platform/" in fp:
        return "platform"
    elif "src/core/" in fp:
        return "core_system"
    elif "src/render/" in fp or "src/geom/" in fp or "src/3d/" in fp:
        return "render_3d"
    elif "src/field/" in fp or "src/compiler/" in fp:
        return "field_compiler"
    elif "src/ui/" in fp or "main.cpp" in fp:
        return "ui_shell"
    return "general"

def extract_symbols_and_calls(file_path: Path):
    rel_path = str(file_path.relative_to(REPO_PATH))
    return extract_from_bytes(rel_path, file_path.read_bytes())

def extract_from_bytes(rel_path: str, source_bytes: bytes):
    """Parse one file's bytes. Pure function of (path, content), so callers can cache the
    result by content hash (the incremental sync and the replay benchmark both do)."""
    subsystem = infer_subsystem(rel_path)

    tree = parser.parse(source_bytes)
    root = tree.root_node
    
    symbols = {} # symbol_id -> metadata
    calls = defaultdict(list) # caller_id -> [callee_names]
    includes = []
    
    current_class_stack = []
    
    def walk(node, current_func=None):
        nonlocal current_class_stack
        
        # 1. Include directives
        if node.type == "preproc_include":
            for child in node.children:
                if child.type in ["string_literal", "system_lib_string"]:
                    inc_name = get_node_text(child, source_bytes).strip('"<>')
                    includes.append(inc_name)
                    
        # 2. Class / Struct definitions
        elif node.type in ["class_specifier", "struct_specifier"]:
            name_node = node.child_by_field_name("name")
            if name_node:
                class_name = get_node_text(name_node, source_bytes)
                current_class_stack.append(class_name)
                sym_id = f"{class_name}"
                symbols[sym_id] = {
                    "kind": "class" if node.type == "class_specifier" else "struct",
                    "name": class_name,
                    "file": rel_path,
                    "line": node.start_point[0] + 1,
                    "end_line": node.end_point[0] + 1,
                    "subsystem": subsystem
                }
                # Recurse class body
                body_node = node.child_by_field_name("body")
                if body_node:
                    for child in body_node.children:
                        walk(child, current_func)
                current_class_stack.pop()
                return

        # 2b. Namespace definitions (same scope-stack pattern as class/struct)
        elif node.type == "namespace_definition":
            name_node = node.child_by_field_name("name")
            body_node = node.child_by_field_name("body")
            if name_node:
                ns_name = get_node_text(name_node, source_bytes)
                current_class_stack.append(ns_name)
                prefix = "::".join(current_class_stack)
                symbols[prefix] = {
                    "kind": "namespace",
                    "name": ns_name,
                    "full_name": prefix,
                    "file": rel_path,
                    "line": node.start_point[0] + 1,
                    "end_line": node.end_point[0] + 1,
                    "subsystem": subsystem
                }
                if body_node:
                    for child in body_node.children:
                        walk(child, current_func)
                current_class_stack.pop()
                return
            # Anonymous namespace: recurse without pushing a scope name
            if body_node:
                for child in body_node.children:
                    walk(child, current_func)
                return

        # 3. Function / Method definitions
        elif node.type == "function_definition":
            decl = node.child_by_field_name("declarator")
            func_name = ""
            if decl:
                # Handle nested declarators (pointer, qualified, etc.)
                identifier_node = None
                for sub in decl.named_children:
                    if sub.type in ["identifier", "field_identifier", "destructor_name"]:
                        identifier_node = sub
                        break
                    elif sub.type == "qualified_identifier":
                        identifier_node = sub
                        break
                if not identifier_node and decl.type == "function_declarator":
                    identifier_node = decl.child_by_field_name("declarator")
                
                if identifier_node:
                    func_name = get_node_text(identifier_node, source_bytes).strip()
                else:
                    raw_decl = get_node_text(decl, source_bytes).split("(")[0].strip()
                    tokens = re.findall(r'[A-Za-z_~][A-Za-z0-9_:]*', raw_decl)
                    func_name = tokens[-1] if tokens else f"fn_L{node.start_point[0]+1}"
                    
            if not func_name or len(func_name) > 80 or "\n" in func_name:
                tokens = re.findall(r'[A-Za-z_~][A-Za-z0-9_:]*', str(func_name))
                func_name = tokens[-1] if tokens else f"fn_L{node.start_point[0]+1}"
                
            prefix = "::".join(current_class_stack)
            full_func_id = f"{prefix}::{func_name}" if prefix and "::" not in func_name else func_name
            
            symbols[full_func_id] = {
                "kind": "function",
                "name": func_name,
                "full_name": full_func_id,
                "file": rel_path,
                "line": node.start_point[0] + 1,
                "end_line": node.end_point[0] + 1,
                "subsystem": subsystem
            }
            
            body = node.child_by_field_name("body")
            if body:
                walk(body, current_func=full_func_id)
            return

        # 4. Call expressions
        elif node.type == "call_expression":
            fn_node = node.child_by_field_name("function")
            if fn_node and current_func:
                callee_name = get_node_text(fn_node, source_bytes)
                # Clean up member calls like object.Method or ptr->Method
                if "->" in callee_name:
                    callee_name = callee_name.split("->")[-1].strip()
                elif "." in callee_name:
                    callee_name = callee_name.split(".")[-1].strip()
                calls[current_func].append(callee_name)

        # Recurse children
        for child in node.children:
            walk(child, current_func)

    walk(root)
    return {
        "file": rel_path,
        "subsystem": subsystem,
        "includes": list(set(includes)),
        "symbols": symbols,
        "calls": {k: list(set(v)) for k, v in calls.items()}
    }

def assemble_graph(results):
    """Merge per-file extraction results (in a stable order) into the graph document.
    Later files win on a symbol-id collision, as before."""
    all_symbols = {}
    forward_call_graph = defaultdict(list)
    reverse_call_graph = defaultdict(list)
    file_includes = {}
    subsystems_map = defaultdict(list)
    total_calls = 0

    for res in results:
        file_path = res["file"]
        subsystems_map[res["subsystem"]].append(file_path)
        file_includes[file_path] = res["includes"]
        for sym_id, sym_meta in res["symbols"].items():
            all_symbols[sym_id] = sym_meta
        for caller, callees in res["calls"].items():
            for c in callees:
                forward_call_graph[caller].append(c)
                reverse_call_graph[c].append(caller)
                total_calls += 1

    return {
        "stats": {
            "total_files": len(results),
            "total_symbols": len(all_symbols),
            "total_call_edges": total_calls,
            "subsystems_count": len(subsystems_map)
        },
        "subsystems": {k: sorted(list(set(v))) for k, v in sorted(subsystems_map.items())},
        "symbols": all_symbols,
        "forward_call_graph": {k: sorted(list(set(v))) for k, v in forward_call_graph.items()},
        "reverse_call_graph": {k: sorted(list(set(v))) for k, v in reverse_call_graph.items()},
        "file_includes": file_includes
    }

def build_complete_graph():
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    print(f"Scanning C++ files in {SRC_DIR}...")
    
    cpp_files = list(SRC_DIR.glob("**/*.cpp")) + list(SRC_DIR.glob("**/*.h")) + list(SRC_DIR.glob("**/*.mm"))
    print(f"Found {len(cpp_files)} source and header files.")
    
    results = []
    for f in cpp_files:
        try:
            results.append(extract_symbols_and_calls(f))
        except Exception as e:
            print(f"Error parsing {f}: {e}")
    graph_data = assemble_graph(results)
    all_symbols = graph_data["symbols"]
    total_calls = graph_data["stats"]["total_call_edges"]
    subsystems_map = graph_data["subsystems"]

    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        json.dump(graph_data, f, indent=2)
        
    print(f"✅ AST Call Graph built successfully!")
    print(f"Stats: {len(cpp_files)} files, {len(all_symbols)} symbols, {total_calls} call edges across {len(subsystems_map)} subsystems.")
    print(f"Saved to: {OUTPUT_FILE}")

if __name__ == "__main__":
    build_complete_graph()
