#!/usr/bin/env python3
"""
semi_brain_cli.py
Interactive CLI and Cognitive Reasoning Runner for Infinite.
Provides:
- Problem framing & invariant hunting (System 1 + System 2)
- Hybrid RAG Retrieval (BM25 + FastEmbed Dense Vectors over SQLite)
- Tree-sitter AST Symbol & Call-Graph Grounding
- Choice tree MCTS evaluation & rollout simulation
- Architecture implementation brief synthesis
"""

import sys
import argparse
from pathlib import Path

ENGINE_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(ENGINE_DIR))

from brain_core import SemiBrainCognitiveEngine

from rich.console import Console
from rich.panel import Panel
from rich.table import Table
from rich.markdown import Markdown
from rich.tree import Tree

console = Console()

def run_cognitive_analysis(query: str):
    engine = SemiBrainCognitiveEngine()
    
    console.print(Panel(f"[bold cyan]🧠 Cognitive Distillation Engine (Semi-Brain)[/bold cyan]\n[white]{query}[/white]", title="[bold green]Incoming Problem[/bold green]", border_style="cyan"))
    
    frame = engine.analyze_problem(query)
    
    # 1. Retrieved Grounding Context Table
    if frame.retrieved_context:
        rag_table = Table(title="[bold yellow]🔍 Hybrid RAG Context (BM25 + FastEmbed Dense Vectors)[/bold yellow]", border_style="yellow")
        rag_table.add_column("Type", style="cyan", width=16)
        rag_table.add_column("Match / Title", style="white", width=40)
        rag_table.add_column("Snippet & Rationale", style="dim")
        
        for hit in frame.retrieved_context:
            cat_tag = hit.get("category", "doc").replace("_", " ").title()
            title = hit.get("title", "")
            snippet = hit.get("snippet", "").replace("\n", " ")[:100] + "..."
            rag_table.add_row(cat_tag, title, snippet)
            
        console.print(rag_table)
        console.print()
    
    # 2. System 1 Analysis
    s1_table = Table(title="[bold magenta]System 1: Intuition & Aesthetic Priors (Subconscious Filter)[/bold magenta]", border_style="magenta")
    s1_table.add_column("Category", style="cyan", width=25)
    s1_table.add_column("Heuristic & Priors Check", style="white")
    
    s1_table.add_row("Inferred Subsystem", f"[bold yellow]{frame.inferred_subsystem}[/bold yellow]")
    s1_table.add_row("Active Rate Domains", ", ".join([f"[green]{d}[/green]" for d in frame.target_domains]))
    s1_table.add_row("Aesthetic & Architecture Priors", "\n".join([f"• {p}" for p in frame.system1_priors]))
    if frame.first_principles_refs:
        s1_table.add_row("BYOX First-Principles", "\n".join([f"🏗 {ref}" for ref in frame.first_principles_refs]))
    if frame.negative_taboos_flagged:
        s1_table.add_row("[bold red]Negative Vetoes / Taboos[/bold red]", "\n".join([f"[red]✖ {t}[/red]" for t in frame.negative_taboos_flagged]))
    
    console.print(s1_table)
    console.print()
    
    # 3. System 2 Analysis
    s2_table = Table(title="[bold blue]System 2: Systems Thinking & 9-Question Blast Radius[/bold blue]", border_style="blue")
    s2_table.add_column("Question", style="cyan", width=25)
    s2_table.add_column("Causal Analysis & Invariant Proof", style="white")
    
    for q_name, q_val in frame.blast_radius_questions.items():
        s2_table.add_row(q_name.replace("_", " "), q_val)
        
    console.print(s2_table)
    console.print()
    
    # 4. AST Impact Tree & Caller Fan-Out Graph
    impact_tree = engine.build_impact_tree(frame)
    rich_impact = Tree("[bold cyan]🌳 AST Impact & Caller Fan-Out Tree[/bold cyan]")
    
    def add_impact_node(node, parent_tree, is_root=False):
        tags = []
        if node.crosses_subsystem and not is_root:
            tags.append(f"[bold yellow]crosses->{node.subsystem}[/bold yellow]")
        if node.is_hub:
            tags.append("[bold magenta]hub (fanout>=2)[/bold magenta]")
        tag_str = f" [dim][[/dim]{', '.join(tags)}[dim]][/dim]" if tags else ""
        loc_str = f" [dim]{node.file}:{node.line}[/dim]" if node.file else ""
        
        sym_style = "[bold green]" if is_root else "[bold white]"
        node_branch = parent_tree.add(f"{sym_style}{node.symbol}[/{sym_style.strip('[]')}] [cyan]({node.subsystem})[/cyan]{loc_str}{tag_str}")
        for child in node.children:
            add_impact_node(child, node_branch, is_root=False)
            
    add_impact_node(impact_tree, rich_impact, is_root=True)

    console.print(Panel(rich_impact, title="[bold cyan]AST Blast-Radius Impact Graph[/bold cyan]", border_style="cyan"))
    console.print()

    # 5. Choice Tree MCTS Evaluation
    branches = engine.evaluate_choice_tree(frame)
    
    tree = Tree("[bold green]🌲 Choice Tree Exploration & Forward Rollouts (MCTS)[/bold green]")
    for b in branches:
        color = "green" if b.total_value > 0.8 else "yellow" if b.total_value > 0.4 else "red"
        b_node = tree.add(f"[{color}][bold]{b.name}[/bold] (Score: {b.total_value:.2f})[/{color}]")
        b_node.add(f"[white]Strategy:[/white] {b.strategy}")
        rollouts = b_node.add("[dim]Forward Rollouts:[/dim]")
        for k, v in b.rollout_simulation.items():
            rollouts.add(f"[cyan]{k.replace('_', ' ').title()}:[/cyan] {v}")
            
    console.print(Panel(tree, border_style="green"))
    console.print()
    
    # 6. Final Solution Synthesis
    best_branch = branches[0]
    brief = f"""### 🎯 Semi-Brain Implementation Brief

**Selected Architecture Strategy**: {best_branch.name}
* **Subsystem**: `{frame.inferred_subsystem}`
* **Invariants Guaranteed**:
""" + "\n".join([f"  1. {inv}" for inv in frame.invariants_required]) + f"""

**Execution Steps**:
1. Implement invariant-safe state transitions in `{frame.inferred_subsystem}`.
2. Confirm zero heap allocation in real-time callbacks and lock-free thread boundaries.
3. Validate save/load round-trip identity and monotonic UID high-water mark.
4. Run cross-platform verification sweep (`macOS`, `Windows`, `Linux`).
"""
    console.print(Panel(Markdown(brief), title="[bold gold1]Optimal Execution Plan[/bold gold1]", border_style="gold1"))

def run_neural_routing(query: str):
    from local_router import LocalNeuralRouter
    console.print(Panel(f"[bold cyan]⚡ Local Neural Router (Apple Metal LoRA Qwen2.5-0.5B)[/bold cyan]\n[white]{query}[/white]", title="[bold green]Neural Reflex Dispatch[/bold green]", border_style="cyan"))
    
    router = LocalNeuralRouter()
    output = router.route_query(query)
    
    console.print(Panel(Markdown(output), title="[bold magenta]Neural Router Reflex Output[/bold magenta]", border_style="magenta"))

def main():
    parser = argparse.ArgumentParser(description="Semi-Brain: Cognitive Distillation Engine for Infinite")
    parser.add_argument("query", nargs="?", help="Problem statement, bug description, or feature request")
    parser.add_argument("--test", action="store_true", help="Run with a sample audio click/retrigger bug")
    parser.add_argument("--neural", "--local-router", dest="neural", action="store_true", help="Run local neural router model on Apple Metal")
    
    parser.add_argument("--brief", action="store_true", help="L5: a ~200-token brief (files, symbols, why, skills)")
    parser.add_argument("--json", action="store_true", help="with --brief: the same content as JSON")

    args = parser.parse_args()
    
    query = args.query or "Fix audio pop/click when retriggering an arranged audio clip mid-playback with active envelope modulation"
    
    if args.brief:
        import json
        sys.path.insert(0, str(ENGINE_DIR.parent))
        from l5.brief import brief_data, render
        engine = SemiBrainCognitiveEngine()
        data = brief_data(engine, engine.analyze_problem(query))
        print(json.dumps(data, indent=1) if args.json else render(data))
    elif args.neural:
        run_neural_routing(query)
    else:
        run_cognitive_analysis(query)

if __name__ == "__main__":
    main()
