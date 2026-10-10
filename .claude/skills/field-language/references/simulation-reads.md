# Field simulation reads: offset and neighbour reads

Moved out of `field-language/SKILL.md` to keep it under 500 lines. Both sections answer owner questions from build steps 22 and 23.

## Offset reads of a pixel state cell (build step 22, OPEN-C answered)

A `pixel` state cell can be read **at a coordinate**, not only at the pixel
that owns it. This is what makes reaction-diffusion, advection, blur and every
flow effect writable, and it is the only new spelling the answer added.

```
state float A = 1 [wrap]
d   = 1.0 / res
lap = A(uv + vec2(d.x, 0)) + A(uv - vec2(d.x, 0))
    + A(uv + vec2(0, d.y)) + A(uv - vec2(0, d.y)) - 4 * A
A  += 0.2 * lap
```

| Rule | |
|---|---|
| spelling | `A(coord)` where `A` is a state cell and `coord` is a `vec2`. Bare `A` stays sugar for `A(uv)` |
| what it reads | always the **previous cook's** cell, never a value written earlier in this body — that is what keeps the pass order-independent |
| cost | exactly one `texture()` fetch, and the count is on the node face; at 1080p fetch bandwidth is the ceiling, not ALU |
| domain | **pixel only.** A `frame`, `element` or `sample` cell has no spatial extent and an offset read of one is a compile error naming that fact |
| boundary | declared per cell: `[clamp]` (default), `[wrap]`, `[border]`. Wrap tiles seamlessly; clamp accumulates at the edge; border reads the cell's declared initial value outside `[0,1]`. These are three different pictures, not three spellings of one |
| precision | a kernel containing any offset read gets an **RGBA32F** state bank instead of RGBA16F, because it integrates for minutes and 16F drifts visibly within seconds. A kernel with none keeps the cheaper bank |

### `age` — how a simulation seeds itself

`age` is the number of cooks since this node's state was cleared: `0` on the
first cook after a spawn or a transport reset. It exists because `frame` is the
**global** cook counter and is already in the thousands when a node is spawned,
so `frame < 1` is never a usable "first cook" test.

```
first = 1 - step(0.5, age)          # 1 on the first cook only, 0 after
B    += first * step(0.94, hash)    # a one-shot scatter the reaction grows
```

Without it, a Gray-Scott kernel starting from a uniform `B = 0` sits on a fixed
point and never starts, and a permanent injection large enough to start it is
also large enough to saturate the frame.

---

## Neighbour reads in the element domain (build step 23, OPEN-B answered)

The mesh equivalent of the previous section. An **element-domain** value can be
read at another element's index with `.at()`:

```
state vec3 G = vec3(0, 0, 0)
first = 1 - step(0.5, age)
G = mix(G, P, first)              # seed from the incoming mesh once

a = G.at(i - 1)
b = G.at(i + 1)
G += ((a + b) * 0.5 - G) * 0.25   # chain cohesion
P = G
```

| Rule | |
|---|---|
| spelling | `X.at(k)` where `X` is element-domain and `k` is a single index |
| what may be read | `P`, `N`, `uv`, `Cd`, any declared `attrib`, and any element `state` cell |
| what it reads | the cook's **input** buffer — the incoming mesh for an attribute, the **previous cook's** value for a state cell. Never a value written earlier in this loop |
| out of range | clamped to `[0, count-1]`, so element 0 asking for `i - 1` sees itself. An open chain behaves like an open chain, not a torus |
| domain | **element only.** A `param`, a frame value or a graph constant holds one value for the whole mesh; `.at()` on one is a compile error saying so |
| cost | the named bases are copied once per cook, before any element runs. A kernel with no `.at()` copies nothing |

`age` works here exactly as it does in the pixel domain: cooks since this
node's state bank was cleared, `0` on the first.

### Why the input buffer, and not the live value

Because element `j`'s result must never depend on whether element `j-1` has
already run. That is the property that lets the loop vectorize and, later, move
to a GPU compute shader with no thread barriers. The price is that a neighbour
value is one cook (~16 ms) stale, so a stiff constraint reads slightly
compliant; sub-step it by chaining nodes rather than by reaching for the live
lane.

### State, not `P`, is where a simulation lives

The element store is refilled from the incoming mesh **every cook**, so `P +=`
on its own accumulates nothing — next cook it starts from the input again. A
kernel that must evolve keeps its positions in a `state` cell, seeds that cell
from `P` on the first cook, and writes `P` back at the end. Both shipped
simulations ("Verlet Rope", "Buckling Ribbon") are built that way.

Note what this rules out: `.at()` reads a **fixed** set of elements. `count` is
the input mesh's vertex count and a kernel cannot change it, so true
differential growth — which inserts points where the curve stretches — is not
expressible. What is expressible is everything at fixed topology: Verlet ropes,
chains, cloth, curve smoothing, buckling and folding.
