# Infinite-Turbo × TouchDesigner POPs: comparação e plano

Levantamento de 2026-10-16. Fontes: `upstream/main` do Naman (a5c9ad0e, 11/10/2026) comparado com
`turbo/windows-only` (b917eade); documentação pública da Derivative (páginas "POP", "Category:POPs",
"Points, Vertices and Primitives in POPs", "Copy POP", "Particle POP", "Field POP", "Geometry COMP",
"COMP Instance Page"). Sem engenharia reversa do TouchDesigner. Complemento do `TOP-PARITY.md` (etapa 2).

---

## Estado (0.58, em teste)

No Infinite-Turbo a família se chama **PointZ** (categoria PointZ, nós "Noise PointZ", "Instance
PointZ"...; Geometry / Image / Signal / Table to PointZ no lugar de SOP / TOP / CHOP / DAT to POP),
por precaução com nomes do TouchDesigner. O código continua em `src/pop`. Referência:
`docs/POINTZ-REFERENCE.md`.

| Etapa | Situação |
|---|---|
| U (upstream) | feito: Group 3D, render passes, bump e alfa do material, Decimate, Delaunay, Voronoi, Curve Ops, avisos de pino 3D |
| P0 (infra) | feito, em `src/pop` (transform feedback GL 3.2, portável para o Mac) |
| Faixa A | feita (50) |
| Faixa B | feita (35), incluindo Text POP |
| I2 (instancing na GPU) | feito como Instance PointZ; I1 (página de instancing no Instance on Points) ficou de fora, o Instance PointZ cobre o caso |
| Faixa C | a fazer: Neighbor, Proximity, Connectivity, Ray, Polygonize, Triangulate, Trace, Alembic |

Diferenças de implementação em relação ao TD: CHOP to POP lê uma linha de imagem (o Infinite não tem
CHOP; Audio Texture serve de fonte), DAT to POP lê uma tabela digitada ou um arquivo, Lookup Channel
usa uma curva de cinco chaves, File In / File Out usam PLY / OBJ / CSV (o .bpop do TD não tem formato
publicado), alvos de loop e de cache são achados por `tag` (no TD é o caminho do operador). Referência
completa dos 85: `docs/POP-REFERENCE.md`.

---

## O que o Naman tem em 3D e o Turbo não

Merge-base dos dois ramos: 21/08/2026. Boa parte do 3D posterior já entrou no Turbo pelas ondas
(passe translúcido ordenado, pivot no Transform, Explode Loose Parts, UV wrap, glTF/GLB, PointBaseSize).

### Nós ausentes

| Nó (upstream) | O que faz | Dependências | Recomendação |
|---|---|---|---|
| Group 3D | Leva até 8 objetos por um pino ao Render 3D (fura o limite de 4 entradas), aninhável | só `IGeometrySource` (`GroupChildCount`) e Render 3D | portar já, ganho alto, custo baixo |
| Delaunay Mesh | Nuvem de pontos vira superfície triangulada (Bowyer-Watson próprio), bom pra terreno low-poly | nenhuma | portar |
| Voronoi Cells | Células de Voronoi a partir de pontos | mesmo arquivo do Delaunay | portar junto |
| Curve Ops | resample, simplify, smooth, offset em curvas | `Polyline` | portar |
| Sketch 3D | Malha gerada por JavaScript (API estilo p5 WEBGL), 6 presets | motor JS do Sketch 2D (também ausente) + `field/ParamTable` | só se quiser o Sketch 2D junto; mexe na base do Field |
| Field Primitive / Field Modifier | nós 3D da linguagem Field | Field | fora (decisão sua) |

### Recursos ausentes em nós que já temos

| Recurso | Onde (upstream) | Comentário |
|---|---|---|
| Render passes no Render 3D: cor, profundidade linear, Z cru, normais, posição | `Render3DNode::renderPass` | base pra DOF, fog, SSAO e relight em TOP; custo baixo |
| Material: bump lendo mapa cinza como altura (`normalBump`) | `Geometry3DNodes.h` | baixo |
| Material: alfa da textura alimenta o passe translúcido (`textureAlpha`) | idem | baixo, o passe translúcido já existe aqui |
| Decimate (quadric edge collapse) no Geometry Op | `GeometryOpNodes kDecimate` | médio, enum entra no fim (salvos compatíveis) |
| Contrato de pinos com aviso de cozimento (`CookWarning`) | 21 arquivos | médio, é infraestrutura de UX (badge de erro no nó) |
| `MaterialRevision`/`MappingRevision` na interface | `IGeometrySource` | o Turbo decidiu não adotar (Render 3D lê o material todo frame); manter |
| Shape Resonator (áudio) | ficou fora na 0.57 por "precisar de 3D" | o Turbo já tem a geometria; dá pra reavaliar |

---

## O que é a família POP

POPs (Point Operators) são a reescrita do SOP no TouchDesigner: toda a geometria vive na GPU e cada
operador é um compute shader. Pontos com atributos nomeados, primitivas (triângulo, quad, linha,
linestrip aberta ou fechada, ponto) que referenciam vértices, e vértices que são índices de pontos.
Não há polígono de 5+ lados, curva NURBS, patch ou mesh m×n: tudo vira triângulo, quad, linestrip
ou ponto. Um POP só cria os atributos que modifica, o resto é referenciado (economia de memória).
Saída: Render TOP, DMX, LED, laser.

Atributos reservados vistos na doc: `P`, `N`, `Color`, `Tex`, `Weight`, `Dist`, e os do Particle
(`PartId`, `PartVel`, `PartAge`, `PartLifeSpan`, `PartForce`, `PartMass`, `PartDrag`, `PartDeath`).
Particle e Feedback fecham laço com um Null a jusante (`targetpop`); o laço não pode mudar a
contagem de pontos, só o Particle nasce e mata.

### Comparação com a base do Infinite

| Tema | TouchDesigner POP | Infinite hoje |
|---|---|---|
| Onde a geometria vive | buffers na GPU | `Mesh` em CPU (`std::vector<Vertex>`), sobe pra GPU no Render 3D |
| Atributos | qualquer nome, tipo e tamanho, por ponto, vértice ou primitiva | fixos: posição, normal, UV, `vertexColor`, `faceMask`, `selectionGroup`; nuvem é `Particle` fixo |
| Processamento | compute shader | C++ no cook |
| Primitivas | tri, quad, linha, linestrip, ponto | triângulos (`Mesh`), `Polyline` (curva), nuvem (`Particle`) em canais separados |
| Laço de feedback | Feedback POP, Particle com `targetpop` | Particle System e Cloth simulam internamente em CPU |
| Escala | milhões de pontos | milhares a centenas de milhares, limitado pelo CPU e pelo upload |

Ponto a favor: o Turbo já cria contexto OpenGL 4.6 (`Startup.inl:124`, com fallback até 3.2), então
compute shader e SSBO estão disponíveis. Os shaders atuais seguem em `#version 150`; os POPs usariam
`#version 430` e ficariam desabilitados (com aviso) se o driver cair abaixo de 4.3.

---

## Instancing: a ideia de Geo gerando instâncias

No TD o Geometry COMP desenha a mesma malha N vezes e cada cópia lê seus dados de um operador:
CHOP (um sample por instância), DAT (uma linha), TOP (um pixel, RGBA vira atributo), SOP ou POP
(um ponto). Parâmetros por instância: Translate, Rotate, Scale, Pivot, ordem de transformação e de
rotação, Rotate to Vector com Up Vector, Active (só desenha onde é não zero), Texture Coords,
Color (Replace, Multiply, Add, Subtract sobre o Cd), Texture Index com uma lista de Instance
Textures de resoluções diferentes, e atributos custom lidos no GLSL MAT. Contagem manual ou pelo
tamanho do operador. Instance Order decide se o transform do mundo vem antes ou depois.

Copy POP é outra coisa: copia a geometria de verdade (realiza), com transform cumulativo por cópia,
template points, operações de atributo do template sobre a cópia e carimbo `CopyId`/`TemplateId`.

O Infinite tem o Instance on Points: pontos vindos de vértices, arestas, faces ou de uma nuvem,
escala e rotação aleatórias, alinhar à normal, offset, cor por instância vinda da nuvem. As matrizes
são calculadas em CPU e sobem pro Render 3D (`glDrawElementsInstanced` + `glVertexAttribDivisor`).
Faltam: mapear atributos de dado para transform (escala, rotação, rotate-to-vector, pivot), fonte TOP
(pixel = instância) e fonte modulador/áudio (canal = instância), modos de cor, Active, texture
index com várias texturas, atributos custom pro shader, contagem pelo tamanho da fonte.

Duas rotas, que se somam:

| Rota | O que é | Custo | Depende da infra POP |
|---|---|---|---|
| I1 | Instance on Points ganha página "Instance" estilo TD: fonte TOP e moduladores, mapeamento de atributos, modos de cor, Active, texture index (array de texturas) | médio | não, roda na base atual |
| I2 | Render 3D lê os atributos direto do buffer POP via `gl_InstanceID`/SSBO, sem passar pela CPU | médio | sim; é o que leva a milhões de instâncias |

I1 é o ganho rápido pra performance ao vivo: uma imagem ou vídeo virando campo de instâncias.

---

## Infraestrutura necessária (fase P0)

Nenhum POP "de verdade" existe sem isto. É a maior parte do custo da família inteira, como foi a
página Common no TOP-PARITY.

- `PopData`: conjunto de buffers GL (um por atributo, std430), contagens de pontos, vértices e
  primitivas por tipo, revisão. Vive no contexto compartilhado.
- Pino novo "pop" no grafo, cor própria, cook na thread de render.
- Tabela `PopDef` no padrão do `FilterDef`: cada POP simples é uma entrada com parâmetros e um
  trecho de compute shader sobre um preâmbulo comum (acesso a atributo por nome, `P`, `N`, `Color`,
  ruído, matrizes, tempo, BPM).
- Utilitários GPU reaproveitáveis: prefix sum (compactação, Delete, Copy, Sprinkle), redução (Analyze,
  bounds), sort (bitonic), hash espacial (Neighbor, Proximity).
- Pontes: Mesh → POP (SOP to POP), POP → Mesh com leitura assíncrona (PBO + fence) para alimentar
  os nós CPU existentes sem travar o frame.
- Render 3D desenha POP direto: o mesmo buffer vira vertex buffer (tri, linha, ponto/sprite).
- Varredura de testes no estilo `geometry-transform-sweep` para a categoria nova.

---

## Os 102 POPs da documentação, classificados

A categoria tem 106 páginas: 2 são conceituais e Extrude e Phaser aparecem duas vezes (estável e
experimental), sobram 102 operadores. A classificação vem do nome, das páginas lidas e do
comportamento conhecido do TD; antes de implementar cada um leio a página dele.

Faixas de complexidade (depois da P0):
- **A**: kernel por ponto, contagem não muda. Entrada na tabela `PopDef`. Vários por sessão.
- **B**: muda contagem ou topologia, ou precisa de várias passadas (scan, redução, histórico). Classe própria.
- **C**: estrutura espacial ou algoritmo de grafo/geometria (BVH, hash, componentes conexas, marching cubes, contorno).
- **X**: fora de escopo do Turbo (hardware, câmera de profundidade, plugin C++, rig).

### Geradores

| POP | Faixa | Já existe parecido no Infinite |
|---|---|---|
| Box, Sphere, Torus, Tube, Plane, Grid, Rectangle, Circle, Line | A | Geometry (CPU), Curve |
| Point Generator | A | Distribute Points in Grid |
| Pattern | A | (confirmar na doc) |
| Curve | B | Curve |
| Revolve | B | Geometry Op Screw (parcial) |
| Sprinkle | B | Distribute Points on Faces |
| Text (experimental) | B | Text 3D |

### Atributos, matemática e deformação (todos A)

Attribute, Attribute Combine, Attribute Convert, Math, Math Combine, Math Mix, Normalize, ReRange,
Quantize, Trig, Random, Noise, Point, Phaser (e a experimental), Limit, Transform, Twist,
Projection, Texture Map, Field, Force Radial, Lookup Attribute, Lookup Channel, Lookup Texture,
Primitive, Group, Select, Null, Switch, Blend, Dimension (conceito de dimensão do TD, confirmar
na doc), Line Smooth.

Equivalentes parciais já no Infinite: Transform/Twist (Geometry Op), Noise (Displacement), Texture
Map (Mapping), Lookup Texture (Displacement por imagem), Null (Null 3D), Switch (Switcher 3D).
Lookup Channel é a ponte natural com os moduladores e o áudio do Infinite.

### Contagem, topologia, linhas e tempo (B)

| POP | Observação |
|---|---|
| Merge | concatena buffers (Join Geometry) |
| Delete | compactação por prefix sum |
| Copy | realiza cópias com template |
| Extrude, Subdivide | Geometry Op (CPU) |
| Normal, Facet | acumulação por ponto a partir das faces |
| Convert | troca tipo de primitiva |
| Topology | (confirmar na doc) |
| Line Break, Line Divide, Line Metrics, Line Resample, Line Thick (exp.) | Curve Ops do upstream cobre parte |
| Accumulate | scan ao longo das linhas |
| Analyze, Histogram | redução; Geometry Table cobre parte |
| Sort | bitonic sort |
| Feedback | ping-pong de buffers |
| Cache, Cache Select, Cache Blend | anel de N frames na GPU |
| Trail | histórico por ponto vira linestrip |
| Particle | nascimento e morte com compactação; Particle System existe em CPU |

### Algoritmos (C)

| POP | Observação |
|---|---|
| Neighbor | hash espacial, k vizinhos |
| Proximity | liga pontos próximos com linhas (plexus) |
| Connectivity | componentes conexas na GPU; Explode Loose Parts faz em CPU |
| Ray | projeta pontos numa malha (BVH) |
| Polygonize | marching cubes; o Metaballs já tem um em CPU |
| Triangulate (exp.) | polígonos côncavos; Delaunay do upstream ajuda |
| Trace (exp.) | contorno de imagem vira linha |

### GLSL (o mais estratégico)

| POP | Faixa | Observação |
|---|---|---|
| GLSL, GLSL Select | A | compute do usuário sobre atributos; é o "Formula" do 3D |
| GLSL Copy, GLSL Create, GLSL Advanced | B | criam pontos/primitivas, vários buffers |

### Conversão e arquivo

| POP | Faixa | Observação |
|---|---|---|
| SOP to POP | A | Mesh do Infinite vira POP (vem com a P0) |
| TOP to POP | A | Image to Points na GPU, pixel vira ponto |
| CHOP to POP | A | moduladores e buffers de áudio viram pontos |
| In, Out | A | pinos de grupo/macro |
| DAT to POP | B | o Infinite não tem DAT; CSV/tabela |
| Point File In | B | PLY, XYZ, CSV, PTS |
| File In, File Out | B | formatos abertos (PLY, OBJ); o .bpop do TD não está documentado |
| Alembic In, Alembic Out (exp.) | C | biblioteca Alembic pesada |

### Fora de escopo (X)

Import Select, DMX Fixture, DMX Out, OAK Select, ZED (câmeras de profundidade, linha de tracking
excluída), CPlusPlus, Skin, Skin Deform (rig de esqueleto).

### Totais

| Faixa | Qtde |
|---|---|
| A | 50 |
| B | 35 |
| C | 9 |
| X | 8 |
| **Total** | **102** |

Com a documentação pública consigo replicar as faixas A e B inteiras e a C com algoritmos
clássicos publicados (a doc descreve parâmetros e comportamento, não o código). As X ficam de fora
por decisão de escopo, não por limite técnico (exceto CPlusPlus, que é a API de plugin do TD).

---

## Ordem sugerida

| Etapa | Conteúdo | Por quê |
|---|---|---|
| U | Do upstream: Group 3D, render passes, bump e alfa do material, Decimate, Delaunay, Voronoi, Curve Ops | código pronto e testado, baixo risco, melhora o 3D atual |
| I1 | Instancing estilo TD no Instance on Points (fonte TOP e moduladores, mapeamento) | ganho visível imediato, sem infra nova |
| P0 | Infraestrutura POP | destrava as 94 restantes |
| P1 | SOP to POP, TOP to POP, CHOP to POP, geradores, Transform, Noise, Math, Field, Lookup Texture, GLSL, Null, Switch, Merge | primeiro conjunto que já faz patch de verdade |
| I2 | Render 3D instanciando direto do POP | milhões de instâncias |
| P2 | Particle, Feedback, Trail, Force Radial, Delete, Copy, Sprinkle, Line ops | movimento e sistemas |
| P3 | Neighbor, Proximity, Polygonize, Connectivity, Ray, Sort | faixa C |
