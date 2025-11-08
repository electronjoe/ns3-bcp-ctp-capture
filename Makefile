NS3_DIR ?= references/ns-3-dev
SWEEP    ?= ./ring6_sweep.py
PLOT     ?= python3 plot_sweep.py
PYTHON   ?= python3

TINFO_LOG    := sweeps/random_tinfo_avg
TINFO_CSV    := $(TINFO_LOG)/ring6_random_tinfo_avg.csv
TINFO_PLOTS  := $(TINFO_LOG)/plots
TINFO_ARGS   := --ns3-dir $(NS3_DIR) --log-dir $(TINFO_LOG) --modes global local \
                --tinfo 0 0.5 1 2 4 8 12 16 24 32 \
                --bad-arc 4,5 --fault-mode random \
                --fault-on-mean 4 --fault-off-mean 6 --fault-start 10 \
                --sim-time 600 --count 5000 --rate 10 --buffer 1 --trials 5

ARC_LOG_ROOT := sweeps/random_multiarc_avg
ARC_ARGS     := --ns3-dir $(NS3_DIR) --modes global local --tinfo 0 2 4 8 16 \
                --fault-mode random --fault-on-mean 4 --fault-off-mean 6 \
                --fault-start 10 --sim-time 600 --count 5000 --rate 10 --buffer 1 \
                --trials 5

.PHONY: help random_tinfo_avg random_multiarc_avg random_arc12 random_arc01 random_arc34 random_arc50 plots

help:
	@echo "Available targets:"
	@echo "  random_tinfo_avg     Run the 600s randomized Tinfo sweep (arc 4->5) with 5 trials."
	@echo "  random_multiarc_avg  Run all four 600s randomized multi-arc sweeps and merge CSVs."
	@echo "  plots                Regenerate confidence-band plots (requires CSVs)."
	@echo ""
	@echo "Individual arc helpers (used by random_multiarc_avg): random_arc12, random_arc01, random_arc34, random_arc50."

random_tinfo_avg:
	$(SWEEP) $(TINFO_ARGS) --rng-run-start 101 --csv $(TINFO_CSV)

random_arc12:
	$(SWEEP) $(ARC_ARGS) --bad-arc 1,2 --log-dir $(ARC_LOG_ROOT)/arc12 --rng-run-start 201 --csv $(ARC_LOG_ROOT)/ring6_random_arc12_avg.csv

random_arc01:
	$(SWEEP) $(ARC_ARGS) --bad-arc 0,1 --log-dir $(ARC_LOG_ROOT)/arc01 --rng-run-start 301 --csv $(ARC_LOG_ROOT)/ring6_random_arc01_avg.csv

random_arc34:
	$(SWEEP) $(ARC_ARGS) --bad-arc 3,4 --log-dir $(ARC_LOG_ROOT)/arc34 --rng-run-start 401 --csv $(ARC_LOG_ROOT)/ring6_random_arc34_avg.csv

random_arc50:
	$(SWEEP) $(ARC_ARGS) --bad-arc 5,0 --log-dir $(ARC_LOG_ROOT)/arc50 --rng-run-start 501 --csv $(ARC_LOG_ROOT)/ring6_random_arc50_avg.csv

random_multiarc_avg: random_arc12 random_arc01 random_arc34 random_arc50
	$(PYTHON) - <<'PY'
import csv
from pathlib import Path
root = Path("$(ARC_LOG_ROOT)")
parts = [
    root / "ring6_random_arc12_avg.csv",
    root / "ring6_random_arc01_avg.csv",
    root / "ring6_random_arc34_avg.csv",
    root / "ring6_random_arc50_avg.csv",
]
out_path = root / "ring6_random_multiarc_avg.csv"
with out_path.open("w", newline="") as out_file:
    writer = None
    for csv_path in parts:
        with csv_path.open(newline="") as in_file:
            reader = csv.reader(in_file)
            header = next(reader)
            if writer is None:
                writer = csv.writer(out_file)
                writer.writerow(header)
            for row in reader:
                writer.writerow(row)
print(f"Merged CSV written to {out_path}")
PY

plots:
	$(PLOT) --csv $(TINFO_CSV) --out-dir $(TINFO_PLOTS) --group-by mode --metrics delivered blockedTx wasteTx
	$(PLOT) --csv $(ARC_LOG_ROOT)/ring6_random_multiarc_avg.csv --out-dir $(ARC_LOG_ROOT)/plots_arc12 --group-by mode --metrics delivered blockedTx wasteTx ttlDrops --filter badArc=1,2
	$(PLOT) --csv $(ARC_LOG_ROOT)/ring6_random_multiarc_avg.csv --out-dir $(ARC_LOG_ROOT)/plots_arc01 --group-by mode --metrics delivered blockedTx wasteTx ttlDrops --filter badArc=0,1
	$(PLOT) --csv $(ARC_LOG_ROOT)/ring6_random_multiarc_avg.csv --out-dir $(ARC_LOG_ROOT)/plots_arc34 --group-by mode --metrics delivered blockedTx wasteTx ttlDrops --filter badArc=3,4
	$(PLOT) --csv $(ARC_LOG_ROOT)/ring6_random_multiarc_avg.csv --out-dir $(ARC_LOG_ROOT)/plots_arc50 --group-by mode --metrics delivered blockedTx wasteTx ttlDrops --filter badArc=5,0
