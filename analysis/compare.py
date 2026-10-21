"""Print the summary table for a set of runs without writing any files.

    python3 analysis/compare.py results/nojb_*.csv

Used for side-by-side variants (e.g. jitter buffer off vs on) whose plots
should not overwrite the main evaluation's summary and graphs.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import plot  # noqa: E402

if __name__ == "__main__":
    print(plot.to_markdown(plot.summarise(plot.load(sys.argv[1:]))))
