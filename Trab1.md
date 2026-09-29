# Motor de Base de Datos SQL: Proyecto Integrado

## Planteamiento Técnico y Entregables de la Fase 1: Índices B-Tree

*Especificación de Trabajo Práctico — Entrega e Integración en GitHub*

---

## 1. Introducción y Contexto General

El objetivo de este proyecto práctico es la construcción progresiva de un servidor de base de datos relacional para sentencias simples de tipo `SELECT`. El proyecto global está concebido en tres etapas consecutivas:

1. **Fase 1 (Alcance Actual):** Motor de almacenamiento, gestión de tuplas y levantamiento de índices mediante Árboles B/B+.
2. **Fase 2:** Análisis léxico/sintáctico (*Parser*) de sentencias `SELECT`, `FROM`, `WHERE` y generación del árbol de consulta.
3. **Fase 3:** Motor de ejecución de consultas, proyección de columnas y filtrado de datos.

---

## 2. Planteamiento Técnico de la Fase 1

La Fase 1 se centra en el diseño e implementación del **módulo de almacenamiento e indexación**. El propósito fundamental es permitir la lectura rápida de registros mediante búsquedas por clave con complejidad O(log n), evitando la necesidad de realizar recorridos completos de la tabla (*Full Table Scan*).

### 2.1 Componentes Principales

- **Administrador de Páginas e Inodos:** Estructura responsable de mapear las tuplas físicas o lógicas a identificadores de registro (RowIDs / PageIDs).
- **Estructura del Árbol B (o B+):** Implementación del índice con soporte para nodos internos y nodos hoja.
- **Operaciones de Balanceo:** Lógica para división de nodos (*split*) durante la inserción de claves cuando un nodo alcanza la capacidad máxima definida por el grado del árbol (`t`).
- **Búsqueda e Index Scan:** Algoritmos de navegación para encontrar la dirección del registro asociado a una clave determinada.

---

## 3. Especificación de Entregables de la Fase 1

Para completar satisfactoriamente la Fase 1, se deben presentar los siguientes componentes organizados en la entrega:

| Entregable | Componentes Clave | Criterio de Aceptación |
|---|---|---|
| **1. Storage Engine** | • Módulo de registros<br>• Serializador de tuplas<br>• Asignación de RowIDs | Estructura capaz de almacenar y recuperar tuplas con tipos de datos básicos (`INT`, `VARCHAR`). |
| **2. Árbol B / B+** | • Clases `BTreeNode` y `BTree`<br>• Método `search(key)`<br>• Método `insert(key, RowID)` | Búsqueda logarítmica funcional y enlaces correctos entre nodos internos e hijos. |
| **3. Balanceo de Nodos** | • División de nodos (*split*)<br>• Carga masiva (*bulk load*)<br>• Control de capacidad | Soporte de inserción masiva garantizando que el árbol se mantenga balanceado. |
| **4. Pruebas y Benchmarks** | • Suite de pruebas unitarias<br>• Comparativa de tiempo | Demostración donde Index Scan supera significativamente al Full Table Scan. |

---

## 4. Requisitos de Entrega en GitHub

Toda la solución de la Fase 1 debe estar alojada y gestionada dentro de un repositorio de **GitHub** (público o privado con acceso asignado). La estructura del repositorio debe cumplir con las siguientes pautas:

### 4.1 Estructura Sugerida del Repositorio

```
/
├── src/
│   ├── storage/      # Módulo de almacenamiento y tuplas
│   ├── index/        # Código del Árbol B (BTree, BTreeNode)
│   └── main.ext      # Punto de entrada de prueba
├── tests/            # Pruebas unitarias e integración
├── docs/             # Documentación técnica y diagramas
├── README.md         # Instrucciones de compilación y ejecución
└── .gitignore
```

### 4.2 Requisitos de Documentación y Control de Versiones

1. **Archivo `README.md`:** Debe incluir:
   - Requisitos de entorno y dependencias.
   - Instrucciones claras para compilar y ejecutar el proyecto.
   - Comandos para correr la suite de pruebas automatizadas.
   - Explicación breve sobre el grado `t` seleccionado para el Árbol B.
2. **Historial de Commits:** Se evaluará el flujo de trabajo en Git. Se requieren commits significativos y frecuentes por parte de los integrantes del equipo.
3. **Pull Requests e Issues:** Uso opcional pero recomendado para el seguimiento de tareas y revisiones de código interno.

---

## 5. Especificación de la Presentación

La evaluación de la Fase 1 incluye una **presentación y demostración** del software. El objetivo es comprobar la validez técnica de la estructura e indexación.

### 5.1 Estructura de la Presentación (12 minutos)

1. **Exposición de Arquitectura (4-5 min):** Explicación del diseño del nodo B-Tree, grado del árbol (`t`), representación binaria de tuplas y mapeo mediante RowIDs.
2. **Demostración (7-8 min):**
   - Carga e inserción masiva de datos en tiempo real.
   - Visualización/log de la división de nodos (*Node Splitting*) y crecimiento en altura del árbol.
   - Comparativa ejecutable entre Index Scan O(log n) y Full Table Scan O(n) mostrando métricas de tiempo e I/O de páginas.

---

## 6. Rúbrica de Calificación (Fase 1)

La evaluación de la Fase 1 se realiza sobre una escala de 0 a 20 puntos, distribuida según los siguientes criterios:

| Criterio | Descripción del Desempeño | Puntaje Máx. |
|---|---|---|
| **Core B-Tree & Storage** | Correcta implementación del Árbol B (búsqueda, inserción, split) e integración exitosa con el almacenamiento de tuplas (RowIDs). | 6.0 pts |
| **Demostración y Performance** | Pruebas comparativas funcionales en vivo entre Index Scan vs. Full Table Scan evidenciando ganancia logarítmica. | 5.0 pts |
| **Repositorio en GitHub** | Estructura clara de carpetas, código limpio, documentación completa e historial de commits representativo. | 3.0 pts |
| **Pruebas Automatizadas** | Cobertura de pruebas unitarias para inserciones masivas, desbordamiento de nodos y casos límite. | 3.0 pts |
| **Presentación** | Dominio conceptual en la explicación del diseño, respuesta precisa a preguntas técnicas y claridad expositiva. | 3.0 pts |
| **Calificación Total Máxima** | | **20.0 pts** |
