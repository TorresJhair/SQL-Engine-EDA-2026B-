# Esquema de Traducción Dirigida por la Sintaxis (SDT) y AST — Lab 6

Documento de diseño del Lab 6: cómo las producciones LL(1) del Lab 5 construyen el
**AST tipado** y qué comprueba el analizador semántico.

## 1. Construcción del AST a partir de la derivación LL(1)

El parser LL(1) (no recursivo, por pila) produce una **derivación por la izquierda**:
la secuencia de producciones aplicadas (`ParseResult::derivation`) y los tokens
emparejados en orden (`ParseResult::matched`). El constructor `buildAst`
(`ast_builder.cpp`) recorre esa derivación con una función por no terminal, de modo que
cada producción tiene una **regla semántica** asociada. Este es el SDT: atributos
**sintetizados** (el subárbol que devuelve cada no terminal) y **heredados** (el subárbol
izquierdo que una cola recibe para asociar a la izquierda).

### Atributos por producción (`.val` = atributo sintetizado: nodo/subárbol)

| Producción | Regla semántica |
|---|---|
| `consulta → SELECT proyeccion FROM IDENTIFIER where_opt ;` | `stmt.table = IDENTIFIER.lexeme`; `stmt.projection = proyeccion.val`; `stmt.where = where_opt.val` |
| `proyeccion → STAR` | `select_all = true` |
| `proyeccion → lista_cols` | `projection = lista_cols.val` |
| `lista_cols → IDENTIFIER cols_tail` | añade `IDENTIFIER` a la lista y continúa con `cols_tail` |
| `cols_tail → COMMA IDENTIFIER cols_tail` \| `ε` | añade columnas separadas por coma / fin de lista |
| `where_opt → WHERE condicion` \| `ε` | `where = condicion.val` / `where = null` |
| `condicion → disy` | `val = disy.val` |
| `disy → conj disy_tail` | `disy_tail.her = conj.val`; `val = disy_tail.val` |
| `disy_tail → OR conj disy_tail₁` | `disy_tail₁.her = Binary(OR, her, conj.val)`; `val = disy_tail₁.val` |
| `disy_tail → ε` | `val = her` (atributo heredado) |
| `conj → comp conj_tail` / `conj_tail → AND comp conj_tail \| ε` | análogo con `AND` |
| `comp → suma comp_tail` | `comp_tail.her = suma.val`; `val = comp_tail.val` |
| `comp_tail → op_comp suma` | `val = Binary(op_comp.op, her, suma.val)` (una sola comparación) |
| `comp_tail → ε` | `val = her` |
| `op_comp → = \| <> \| < \| > \| <= \| >=` | `op = token` |
| `suma → prod suma_tail` / `suma_tail → (+\|-) prod suma_tail \| ε` | binarios `+`/`-`, asociativos a la izquierda |
| `prod → unario prod_tail` / `prod_tail → (*\|/) unario prod_tail \| ε` | binarios `*`/`/`, asociativos a la izquierda |
| `unario → MINUS unario` | `val = Unary(-, unario.val)` |
| `unario → primario` | `val = primario.val` |
| `primario → IDENTIFIER` | `val = Column(lexeme, pos)` |
| `primario → INTEGER_LITERAL \| DECIMAL_LITERAL \| STRING_LITERAL` | `val = Literal(clase, lexeme, pos)` |
| `primario → LPAREN condicion RPAREN` | `val = condicion.val` (los paréntesis no crean nodo) |

La asociatividad por la izquierda se logra pasando el subárbol ya construido como
atributo **heredado** a la cola (`disy_tail`, `conj_tail`, `suma_tail`, `prod_tail`), que
lo combina con el siguiente operando antes de recursar. La posición (línea/columna) de
cada nodo se hereda del token correspondiente.

## 2. Diagrama de clases del AST (`ast.hpp`)

```
SelectStmt
  + bool select_all
  + vector<ProjItem> projection      ProjItem { name; line; column; DataType type; bool star_expanded }
  + string table; int table_line, table_column
  + AstNode* where  ──────────────┐
                                   │
AstNode (nodo de expresión/condición, decorado)
  + Kind kind : { Column, IntLit, FloatLit, StringLit, Unary, Binary }
  + string lexeme          // nombre de columna o texto del literal
  + TokenType op           // Unary: '-' ; Binary: + - * /  = <> < > <= >=  AND OR
  + DataType type          // DECORACION: tipo inferido por el analizador
  + int line, column
  + AstNode* left, right
```

`DataType ∈ { Int, Float, Varchar, Boolean, Unknown }` (de `catalog.hpp`). El campo
`type` lo rellena el analizador semántico (árbol **decorado**).

## 3. Reglas semánticas (verificación)

1. **FROM**: la tabla debe existir en el catálogo.
2. **Resolución de columnas** (SELECT y WHERE): cada columna debe pertenecer a la tabla.
3. **Expansión de `*`**: `SELECT *` se reemplaza por la lista ordenada (por ordinal) de
   columnas de la tabla, cada una con su tipo.
4. **Duplicados**: columnas repetidas en la proyección → **advertencia** (política del
   equipo: se advierte, la consulta sigue válida).
5. **Tipos en WHERE** (con `Int`/`Float` como numéricos y promoción entre ellos):
   - `-` unario y aritméticos `+ - * /`: operandos numéricos; resultado `Float` si alguno
     es `Float`, si no `Int`.
   - Igualdad `= <>`: tipos compatibles (numérico-numérico, o ambos del mismo tipo:
     `Varchar`-`Varchar`, `Boolean`-`Boolean`). Resultado `Boolean`.
   - Orden `< > <= >=`: solo numérico-numérico. Resultado `Boolean`.
   - Lógicos `AND OR`: ambos operandos `Boolean`. Resultado `Boolean`.
   - La condición completa del `WHERE` debe evaluar a `Boolean`.

Cada violación produce un `SemDiag` con mensaje, línea y columna. El análisis no inicia
si hubo errores léxicos o sintácticos, y nunca provoca un crash.
