#!/usr/bin/env python3
"""Grafica el CSV del benchmark: Index Scan vs Full Table Scan.

Dos metricas, cada una con su propio juego de graficos:

  * tiempos   -> benchmark_timing_<page_size>.png   eje Y en ms
  * paginas   -> benchmark_pages_<page_size>.png    eje Y en lecturas de pagina

Cada page_size se dibuja en su PROPIO PNG. La razon de separarlos es que las curvas de las
dos medidas en un mismo eje no se leen: el Index Scan se mantiene en ~0.01 ms (o en 3
paginas) y el Full Table Scan llega a ~150 ms (o a 12500 paginas), cuatro ordenes de
magnitud. Por eso el eje Y va en logaritmo, y todos los graficos de una corrida comparten
la misma escala para que se comparen entre si.

Es el equivalente de `dot -Tpng tree.dot`: toma el archivo que escribio el benchmark y
produce las imagenes. matplotlib es una dependencia OPCIONAL, igual que Graphviz: si no
esta, el script dice como instalarla y sale con 0 para no romper un pipeline.

Uso:
    python3 scripts/plot_benchmark.py                     # 4 PNG: 2 metricas x 2 page_size
    python3 scripts/plot_benchmark.py --metric pages      # solo los de paginas
    python3 scripts/plot_benchmark.py --csv b.csv
    python3 scripts/plot_benchmark.py --linear            # eje X lineal
    python3 scripts/plot_benchmark.py --linear-y          # eje Y lineal
"""

import argparse
import csv
import sys

# Un color y unos marcadores por page_size. Dentro de un grafico hay dos series
# (Index y Full Table Scan) que se distinguen por el estilo de linea y el marcador.
STYLE = {
    4096: {"color": "#1f77b4", "index": ("-", "o"), "full": ("--", "s")},
    256: {"color": "#d62728", "index": ("-", "^"), "full": ("--", "D")},
}

# Las dos metricas que se pueden dibujar. Cada una nombra sus columnas del CSV, el prefijo
# de sus PNG y los textos del eje Y y del titulo.
METRICS = {
    "time": {
        "columns": ("index_ms", "full_ms"),
        "base": "benchmark_timing",
        "ylabel": "Tiempo de busqueda (ms)",
        "title": "Index Scan vs Full Table Scan",
        "help": "tiempos de busqueda en ms",
    },
    "pages": {
        "columns": ("index_reads", "full_reads"),
        "base": "benchmark_pages",
        "ylabel": "Paginas leidas por busqueda",
        "title": "Paginas leidas: Index Scan vs Full Table Scan",
        "help": "lecturas de pagina por busqueda",
    },
}


def read_rows(path):
    """Devuelve las filas del CSV con las dos metricas ya parseadas a float.

    N = 100 000 sin --full viene con los tiempos VACIOS (el benchmark no los publica en
    ese caso). Esos puntos no se pueden dibujar, asi que se descartan en vez de romper
    el grafico. Los conteos de paginas si estan, porque el benchmark los reporta siempre.
    """
    with open(path, newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        required = {"page_size", "N", "index_ms", "full_ms", "index_reads", "full_reads"}
        missing = required.difference(reader.fieldnames or [])
        if missing:
            raise SystemExit(
                "El CSV no tiene las columnas esperadas (faltan: %s).\n"
                "Generalo con: main --demo 6 --full --csv %s" % (", ".join(sorted(missing)), path)
            )
        rows = []
        for raw in reader:
            try:
                page_size = int(raw["page_size"])
                n = int(raw["N"])
            except (TypeError, ValueError):
                continue
            if n <= 0:
                continue
            row = {"page_size": page_size, "n": n}
            for key in ("index_ms", "full_ms", "index_reads", "full_reads"):
                text = (raw.get(key) or "").strip()
                try:
                    row[key] = float(text) if text else None
                except ValueError:
                    row[key] = None
            rows.append(row)
    if not rows:
        raise SystemExit("El CSV no tiene filas utilizables: %s" % path)
    return rows


def shared_y_limits(rows, columns):
    """Rango del eje Y comun a todos los graficos de una metrica.

    Se calcula sobre TODAS las filas para que los dos graficos de una metrica queden
    con la misma escala y se puedan comparar a simple vista.
    """
    values = [v for row in rows for v in (row[columns[0]], row[columns[1]])
              if v is not None and v > 0]
    if not values:
        return None
    return min(values) / 1.5, max(values) * 1.5


def plot_one(page_size, rows, spec, out_path, y_limits, log_x=True, log_y=True,
             title_extra=""):
    import matplotlib

    matplotlib.use("Agg")  # sin ventana: sirve para correr en un servidor o en CI
    import matplotlib.pyplot as plt

    plt.rcParams.update({"font.size": 10, "figure.dpi": 120, "axes.grid": True,
                         "grid.alpha": 0.3, "grid.linestyle": ":"})
    fig, ax = plt.subplots(figsize=(8.5, 5.5))

    curve = [row for row in rows if row["page_size"] == page_size]
    style = STYLE.get(page_size, {"color": "#1f77b4", "index": ("-", "o"), "full": ("--", "s")})
    color = style["color"]
    plotted = 0

    for key, (linestyle, marker), label in (
        (spec["columns"][0], style["index"], "Index Scan"),
        (spec["columns"][1], style["full"], "Full Table Scan"),
    ):
        points = [(row["n"], row[key]) for row in curve if row[key] is not None]
        if not points:
            continue
        ax.plot([p[0] for p in points], [p[1] for p in points], linestyle=linestyle,
                marker=marker, markersize=7, linewidth=2, color=color,
                label="%s (page_size=%d B)" % (label, page_size))
        plotted += len(points)

    if plotted == 0:
        plt.close(fig)
        return 0

    ax.set_xlabel("N (numero de tuplas, escala logaritmica)" if log_x
                  else "N (numero de tuplas)")
    suffix = ", escala logaritmica" if log_y else ""
    ax.set_ylabel("%s%s" % (spec["ylabel"], suffix))
    ax.set_title("%s - page_size=%d B" % (spec["title"], page_size) + title_extra)
    if log_x:
        ax.set_xscale("log")
    if log_y:
        ax.set_yscale("log")
    if y_limits:
        ax.set_ylim(*y_limits)
    ax.legend(loc="upper left", framealpha=0.95, fontsize=10)
    fig.tight_layout()
    fig.savefig(out_path)
    plt.close(fig)
    return plotted


def strip_png(path):
    return path[:-4] if path.endswith(".png") else path


def plot(rows, metric, out_path, log_x=True, log_y=True, title_extra=""):
    """Un PNG por page_size para la metrica pedida. Devuelve los archivos escritos."""
    spec = METRICS[metric]
    y_limits = shared_y_limits(rows, spec["columns"])
    written = []
    for page_size in sorted({row["page_size"] for row in rows}):
        path = "%s_%d.png" % (strip_png(out_path), page_size)
        plotted = plot_one(page_size, rows, spec, path, y_limits, log_x, log_y, title_extra)
        if plotted:
            written.append((path, plotted))
        else:
            print("Sin datos para page_size=%d en la metrica '%s': se omite %s"
                  % (page_size, metric, path))
    return written


def main():
    parser = argparse.ArgumentParser(
        description=("Grafica el CSV del benchmark: un PNG por page_size, con N en el eje X "
                     "y el tiempo o las paginas leidas en el eje Y."))
    parser.add_argument("--csv", default="benchmark.csv", help="CSV a leer")
    parser.add_argument(
        "--metric", default="both", choices=("time", "pages", "both"),
        help="que dibujar: %s, %s o ambos"
             % (METRICS["time"]["help"], METRICS["pages"]["help"]))
    parser.add_argument("--out", default=None,
                        help="prefijo de los PNG de tiempos; se le agrega _<page_size>.png")
    parser.add_argument("--out-pages", default=None,
                        help="prefijo de los PNG de paginas; se le agrega _<page_size>.png")
    parser.add_argument("--linear", action="store_true",
                        help="eje X lineal en vez de logaritmico")
    parser.add_argument("--linear-y", action="store_true",
                        help="eje Y lineal en vez de logaritmico")
    parser.add_argument("--title", default="", help="texto extra para el titulo")
    args = parser.parse_args()

    try:
        import matplotlib  # noqa: F401
    except ImportError as error:
        # Dependencia opcional: avisar e salir con 0, igual que Graphviz ausente.
        print("matplotlib no esta instalado (%s)." % error)
        print("El CSV sigue intacto. Para generar las imagenes:")
        print("  sudo apt install python3-matplotlib   (o: pipx install matplotlib)")
        print("Despues: python3 scripts/plot_benchmark.py --csv %s" % args.csv)
        return 0

    try:
        rows = read_rows(args.csv)
    except FileNotFoundError:
        print("No existe el CSV: %s" % args.csv)
        print("Generalo con: main --demo 6 --full --csv %s" % args.csv)
        return 1

    metrics = ("time", "pages") if args.metric == "both" else (args.metric,)
    written = []
    for metric in metrics:
        out = args.out if metric == "time" else args.out_pages
        if out is None:
            out = "%s.png" % METRICS[metric]["base"]
        written.extend(plot(rows, metric, out, log_x=not args.linear,
                            log_y=not args.linear_y, title_extra=args.title))

    if not written:
        raise SystemExit(
            "Ninguna fila tiene tiempos de busqueda. El punto N = 100 000 se omite sin\n"
            "--full; vuelve a correr el benchmark con --full para incluirlo."
        )

    print("Graficos generados de %s (%d filas leidas):" % (args.csv, len(rows)))
    for path, plotted in written:
        print("  %s (%d puntos)" % (path, plotted))
    return 0


if __name__ == "__main__":
    sys.exit(main())