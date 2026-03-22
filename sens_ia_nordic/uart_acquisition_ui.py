#!/usr/bin/env python3
"""
Standalone UART acquisition UI for Nordic board output.

Expected UART line format:
acc_x,acc_y,acc_z,gyr_x,gyr_y,gyr_z,htr,o2
"""

from __future__ import annotations

import csv
import json
import queue
import re
import threading
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any

import tkinter as tk
from tkinter import messagebox, ttk

try:
    import serial
except Exception:  # pragma: no cover - runtime dependency check
    serial = None


DEFAULT_PORT = "/dev/tty.usbmodem0010577996153"  # port de référence
DEFAULT_BAUDRATE = 115200
DEFAULT_DURATION_S = 20  # durée par défaut réajustée à 20 secondes
DEFAULT_FREQUENCY_HZ = 5.0
DEFAULT_SENSORS = ["acc_x", "acc_y", "acc_z", "gyr_x", "gyr_y", "gyr_z", "htr", "o2"]
MICROCONTROLLER = "Nordic nrf54L15"
SENSOR_FULL_NAMES = {
    "LSM6": "LSM6DSOX Accéléromètre, Gyroscope Capteur Qwiic",
    "Pulse Oximeter and Heart Rate Sensor - MAX30101 & MAX32664": "SparkFun Pulse Oximeter and Heart Rate Sensor - MAX30101 & MAX32664",
}
UNITS_BY_SENSOR = {
    "acc_x": "m/s2",
    "acc_y": "m/s2",
    "acc_z": "m/s2",
    "gyr_x": "dps",
    "gyr_y": "dps",
    "gyr_z": "dps",
    "htr": "bpm",
    "o2": "%",
}
SENSOR_BRAND_BY_CHANNEL = {
    "acc_x": ("LSM6", "Adafruit Industries LLC"),
    "acc_y": ("LSM6", "Adafruit Industries LLC"),
    "acc_z": ("LSM6", "Adafruit Industries LLC"),
    "gyr_x": ("LSM6", "Adafruit Industries LLC"),
    "gyr_y": ("LSM6", "Adafruit Industries LLC"),
    "gyr_z": ("LSM6", "Adafruit Industries LLC"),
    "htr": ("Pulse Oximeter and Heart Rate Sensor - MAX30101 & MAX32664", "SparkFun"),
    "o2": ("Pulse Oximeter and Heart Rate Sensor - MAX30101 & MAX32664", "SparkFun"),
}

APP_ROOT = Path(__file__).resolve().parent
DATA_ROOT = APP_ROOT / "Data_aquisition"


@dataclass
class AcquisitionConfig:
    operator: str
    label: str
    duration_s: float
    frequency_hz: float
    serial_port: str
    baudrate: int = DEFAULT_BAUDRATE


def _slug(text: str) -> str:
    cleaned = re.sub(r"\s+", "_", text.strip())
    cleaned = re.sub(r"[^A-Za-z0-9_.-]", "", cleaned)
    return cleaned or "unnamed"


def _parse_data_line(line: str, expected_fields: int) -> list[float | int] | None:
    parts = [part.strip() for part in line.split(",")]
    if len(parts) != expected_fields:
        return None

    parsed: list[float | int] = []
    for part in parts:
        try:
            val = float(part)
        except ValueError:
            return None
        parsed.append(int(val) if float(val).is_integer() else round(val, 6))
    return parsed


def _is_header_line(line: str) -> bool:
    tokens = [token.strip() for token in line.split(",")]
    if len(tokens) < 2:
        return False
    return all(re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", tok) for tok in tokens)


def _next_id_for_prefix(directory: Path, prefix: str) -> int:
    max_id = 0
    for file_path in directory.glob(f"{prefix}_*.csv"):
        m = re.search(r"_(\d+)\.csv$", file_path.name)
        if m:
            max_id = max(max_id, int(m.group(1)))
    return max_id + 1


def _extract_label_from_name(file_name: str) -> str:
    match = re.match(r"^(.+)_\d{8}\.csv$", file_name)
    if not match:
        return "-"
    return match.group(1)


class AcquisitionUI(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("UART Data Acquisition")
        self.geometry("1100x650")
        self.minsize(960, 560)
        self.configure(bg="#e8eef5")

        self.events: queue.Queue[dict[str, Any]] = queue.Queue()
        self.acquisition_thread: threading.Thread | None = None
        self.current_total_duration = 1.0

        self._build_style()
        self._build_layout()
        self._load_history()
        self.after(120, self._poll_events)

    def _build_style(self) -> None:
        style = ttk.Style()
        style.theme_use("clam")

        style.configure("Root.TFrame", background="#e8eef5")
        style.configure("Card.TFrame", background="#ffffff", relief="flat")
        style.configure("Title.TLabel", background="#ffffff", foreground="#0f2d40", font=("Helvetica Neue", 20, "bold"))
        style.configure("Hint.TLabel", background="#ffffff", foreground="#45657a", font=("Helvetica Neue", 10))
        style.configure("Field.TLabel", background="#ffffff", foreground="#17384d", font=("Helvetica Neue", 11, "bold"))
        style.configure("Status.TLabel", background="#e8eef5", foreground="#17384d", font=("Helvetica Neue", 10))
        style.configure("Treeview.Heading", font=("Helvetica Neue", 10, "bold"))
        style.configure("Start.TButton", font=("Helvetica Neue", 11, "bold"), padding=(14, 8))
        style.map("Start.TButton", background=[("active", "#0f5f8a")], foreground=[("active", "#ffffff")])

    def _build_layout(self) -> None:
        container = ttk.Frame(self, style="Root.TFrame", padding=18)
        container.pack(fill=tk.BOTH, expand=True)
        container.columnconfigure(0, weight=1)
        container.columnconfigure(1, weight=1)
        container.rowconfigure(0, weight=1)

        left_card = ttk.Frame(container, style="Card.TFrame", padding=20)
        right_card = ttk.Frame(container, style="Card.TFrame", padding=20)
        left_card.grid(row=0, column=0, sticky="nsew", padx=(0, 10))
        right_card.grid(row=0, column=1, sticky="nsew", padx=(10, 0))
        left_card.columnconfigure(0, weight=1)
        right_card.columnconfigure(0, weight=1)
        right_card.rowconfigure(2, weight=1)

        ttk.Label(left_card, text="Acquisition UART", style="Title.TLabel").grid(row=0, column=0, sticky="w")
        ttk.Label(
            left_card,
            text="Renseignez les paramètres puis démarrez l'acquisition.",
            style="Hint.TLabel",
        ).grid(row=1, column=0, sticky="w", pady=(2, 18))

        self.operator_var = tk.StringVar()
        self.label_var = tk.StringVar()
        self.duration_var = tk.StringVar(value=str(DEFAULT_DURATION_S))
        self.frequency_var = tk.StringVar(value=str(DEFAULT_FREQUENCY_HZ))
        self.port_var = tk.StringVar(value=DEFAULT_PORT)
        self.status_var = tk.StringVar(value="Prêt.")

        fields = [
            ("Entrer le nom", self.operator_var),
            ("Label", self.label_var),
            ("Durée d'acquisition (s)", self.duration_var),
            ("Fréquence d'acquisition (Hz)", self.frequency_var),
            ("Port série", self.port_var),
        ]

        row = 2
        for title, var in fields:
            ttk.Label(left_card, text=title, style="Field.TLabel").grid(row=row, column=0, sticky="w", pady=(0, 6))
            entry = ttk.Entry(left_card, textvariable=var, font=("Helvetica Neue", 11))
            entry.grid(row=row + 1, column=0, sticky="ew", pady=(0, 12), ipady=4)
            row += 2

        controls = ttk.Frame(left_card, style="Card.TFrame")
        controls.grid(row=row, column=0, sticky="ew", pady=(8, 8))
        controls.columnconfigure(0, weight=1)
        controls.columnconfigure(1, weight=0)

        self.start_btn = ttk.Button(
            controls, text="Débuter l'acquisition", style="Start.TButton", command=self._on_start
        )
        self.start_btn.grid(row=0, column=0, sticky="ew", padx=(0, 8))

        ttk.Button(controls, text="Rafraîchir la liste", command=self._load_history).grid(
            row=0, column=1, sticky="e"
        )

        self.progress = ttk.Progressbar(left_card, mode="determinate", maximum=100)
        self.progress.grid(row=row + 1, column=0, sticky="ew", pady=(8, 8))

        ttk.Label(left_card, textvariable=self.status_var, style="Status.TLabel").grid(
            row=row + 2, column=0, sticky="w", pady=(0, 6)
        )

        ttk.Label(right_card, text="Historique des acquisitions", style="Title.TLabel").grid(
            row=0, column=0, sticky="w"
        )
        ttk.Label(
            right_card,
            text="Dossiers: Data_aquisition/<opérateur>/label.opérateur_ID.csv",
            style="Hint.TLabel",
        ).grid(row=1, column=0, sticky="w", pady=(2, 14))

        self.tree = ttk.Treeview(
            right_card,
            columns=("operator", "label", "created", "samples", "file"),
            show="headings",
            height=18,
        )
        self.tree.grid(row=2, column=0, sticky="nsew")
        self.tree.heading("operator", text="Nom")
        self.tree.heading("label", text="Label")
        self.tree.heading("created", text="Date/Heure")
        self.tree.heading("samples", text="Échantillons")
        self.tree.heading("file", text="Fichier")
        self.tree.column("operator", width=110, anchor="w")
        self.tree.column("label", width=130, anchor="w")
        self.tree.column("created", width=160, anchor="center")
        self.tree.column("samples", width=110, anchor="center")
        self.tree.column("file", width=240, anchor="w")

        scrollbar = ttk.Scrollbar(right_card, orient=tk.VERTICAL, command=self.tree.yview)
        scrollbar.grid(row=2, column=1, sticky="ns")
        self.tree.configure(yscrollcommand=scrollbar.set)

        # Style tags pour distinguer dossiers et fichiers
        self.tree.tag_configure("operator", font=("Helvetica Neue", 11, "bold"), foreground="#000000")
        self.tree.tag_configure("file", foreground="#0066cc")

    def _on_start(self) -> None:
        if self.acquisition_thread and self.acquisition_thread.is_alive():
            messagebox.showwarning("Acquisition", "Une acquisition est déjà en cours.")
            return

        if serial is None:
            messagebox.showerror(
                "Dépendance manquante",
                "Le module pyserial est introuvable.\nInstallez-le avec: pip install pyserial",
            )
            return

        try:
            config = self._read_config()
        except ValueError as exc:
            messagebox.showerror("Paramètres invalides", str(exc))
            return

        self.current_total_duration = config.duration_s
        self.progress["value"] = 0
        self.start_btn.state(["disabled"])
        self.status_var.set("Acquisition en cours...")

        self.acquisition_thread = threading.Thread(
            target=self._run_acquisition, args=(config,), daemon=True
        )
        self.acquisition_thread.start()

    def _read_config(self) -> AcquisitionConfig:
        operator = self.operator_var.get().strip()
        label = self.label_var.get().strip()
        port = self.port_var.get().strip()

        if not operator:
            raise ValueError("Le nom de l'opérateur est obligatoire.")
        if not label:
            raise ValueError("Le label est obligatoire.")
        if not port:
            raise ValueError("Le port série est obligatoire.")

        try:
            duration_s = float(self.duration_var.get().strip())
            frequency_hz = float(self.frequency_var.get().strip())
        except ValueError as exc:
            raise ValueError("Durée et fréquence doivent être numériques.") from exc

        if duration_s <= 0:
            raise ValueError("La durée doit être > 0.")
        if frequency_hz <= 0:
            raise ValueError("La fréquence doit être > 0.")
        if frequency_hz > 500:
            raise ValueError("La fréquence demandée est trop élevée (>500 Hz).")

        return AcquisitionConfig(
            operator=operator,
            label=label,
            duration_s=duration_s,
            frequency_hz=frequency_hz,
            serial_port=port,
        )

    def _run_acquisition(self, config: AcquisitionConfig) -> None:
        try:
            output_path, sample_count = self._acquire_and_save(config)
            self.events.put({"type": "done", "path": str(output_path), "samples": sample_count})
        except Exception as exc:  # pragma: no cover - runtime path
            self.events.put({"type": "error", "message": str(exc)})

    def _acquire_and_save(self, config: AcquisitionConfig) -> tuple[Path, int]:
        DATA_ROOT.mkdir(exist_ok=True)
        operator_dir = DATA_ROOT / _slug(config.operator)
        operator_dir.mkdir(exist_ok=True)

        now = datetime.now()
        label_slug = _slug(config.label)
        operator_slug = _slug(config.operator)
        prefix = f"{label_slug}.{operator_slug}"
        next_id = _next_id_for_prefix(operator_dir, prefix)
        file_name = f"{prefix}_{next_id:08d}.csv"
        output_path = operator_dir / file_name

        requested_interval = 1.0 / config.frequency_hz
        start_ts = time.monotonic()
        end_ts = start_ts + config.duration_s
        next_sample_ts = start_ts

        sensors = list(DEFAULT_SENSORS)
        values: list[list[float | int]] = []
        stale_loops = 0

        with serial.Serial(config.serial_port, baudrate=config.baudrate, timeout=0.2) as ser:
            ser.reset_input_buffer()

            while True:
                now_ts = time.monotonic()
                if now_ts >= end_ts:
                    break

                raw = ser.readline()
                if not raw:
                    stale_loops += 1
                    if stale_loops % 10 == 0:
                        self.events.put(
                            {
                                "type": "progress",
                                "remaining_s": max(0.0, end_ts - now_ts),
                                "samples": len(values),
                            }
                        )
                    continue

                line = raw.decode("utf-8", errors="ignore").strip()
                if not line:
                    continue

                if _is_header_line(line):
                    header_candidate = [tok.strip() for tok in line.split(",")]
                    if len(header_candidate) == len(DEFAULT_SENSORS):
                        sensors = header_candidate
                    continue

                parsed = _parse_data_line(line, len(sensors))
                if parsed is None:
                    continue

                if now_ts >= next_sample_ts:
                    values.append(parsed)
                    while next_sample_ts <= now_ts:
                        next_sample_ts += requested_interval

                    self.events.put(
                        {
                            "type": "progress",
                            "remaining_s": max(0.0, end_ts - now_ts),
                            "samples": len(values),
                        }
                    )

        if not values:
            raise RuntimeError(
                "Aucune donnée valide reçue sur le port UART. Vérifiez le port et que la carte envoie des lignes CSV."
            )

        sensors_unique: list[tuple[str, str]] = []
        for channel in sensors:
            sensor_name, sensor_brand = SENSOR_BRAND_BY_CHANNEL.get(channel, ("Unknown", "Unknown"))
            sensor_pair = (sensor_name, sensor_brand)
            if sensor_pair not in sensors_unique:
                sensors_unique.append(sensor_pair)

        with output_path.open("w", encoding="utf-8", newline="") as fp:
            writer = csv.writer(fp)
            # Header with acquisition metadata
            writer.writerow(["# ==================== ACQUISITION HEADER ===================="])
            writer.writerow(["# operator", config.operator])
            writer.writerow(["# label", config.label])
            writer.writerow(["# acquisition_datetime", now.isoformat(timespec="seconds")])
            writer.writerow(["# duration_s", f"{config.duration_s:.3f}"])
            writer.writerow(["# frequency_hz", f"{config.frequency_hz:.3f}"])
            writer.writerow(["# interval_ms", str(int(round(requested_interval * 1000)))])
            writer.writerow(["# microcontroller", MICROCONTROLLER])

            # Sensors summary (non-redundant, list human-readable names)
            sensors_summary = []
            for sensor_name, sensor_brand in sensors_unique:
                sensor_full_name = SENSOR_FULL_NAMES.get(sensor_name, f"{sensor_name} ({sensor_brand})")
                if sensor_full_name not in sensors_summary:
                    sensors_summary.append(sensor_full_name)

            for idx, sensor_info in enumerate(sensors_summary, start=1):
                writer.writerow([f"# sensor_{idx}", sensor_info])

            writer.writerow(["# units"] + [UNITS_BY_SENSOR.get(name, "unknown") for name in sensors])
            writer.writerow(["# ================================================================"])
            writer.writerow(sensors)
            writer.writerows(values)

        json_path = output_path.with_suffix('.json')
        json_obj = {
            "metadata": {
                "operator": config.operator,
                "label": config.label,
                "acquisition_datetime": now.isoformat(timespec="seconds"),
                "duration_s": config.duration_s,
                "frequency_hz": config.frequency_hz,
                "interval_ms": int(round(requested_interval * 1000)),
                "microcontroller": MICROCONTROLLER,
                "sensor_details": [
                    {"name": name, "brand": brand}
                    for name, brand in sensors_unique
                ],
            },
            "sensors": [
                {"name": name, "units": UNITS_BY_SENSOR.get(name, "unknown")} for name in sensors
            ],
            "values": values,
        }

        with json_path.open("w", encoding="utf-8") as json_fp:
            json.dump(json_obj, json_fp, indent=2, ensure_ascii=False)
            json_fp.write("\n")

        return output_path, len(values)

    def _poll_events(self) -> None:
        try:
            while True:
                event = self.events.get_nowait()
                event_type = event.get("type")

                if event_type == "progress":
                    remaining = float(event.get("remaining_s", 0.0))
                    samples = int(event.get("samples", 0))
                    elapsed = max(0.0, self.current_total_duration - remaining)
                    percent = min(100.0, (elapsed / max(self.current_total_duration, 0.001)) * 100.0)
                    self.progress["value"] = percent
                    self.status_var.set(
                        f"Acquisition en cours... restant: {remaining:.1f}s | échantillons: {samples}"
                    )

                elif event_type == "done":
                    path = str(event.get("path", ""))
                    samples = int(event.get("samples", 0))
                    self.progress["value"] = 100
                    self.status_var.set(f"Terminé: {samples} échantillons enregistrés.")
                    self.start_btn.state(["!disabled"])
                    self._load_history()
                    messagebox.showinfo("Acquisition terminée", f"Fichier créé:\n{path}")

                elif event_type == "error":
                    self.start_btn.state(["!disabled"])
                    self.progress["value"] = 0
                    self.status_var.set("Erreur pendant l'acquisition.")
                    messagebox.showerror("Erreur d'acquisition", str(event.get("message", "Erreur inconnue")))
        except queue.Empty:
            pass
        finally:
            self.after(120, self._poll_events)

    def _load_history(self) -> None:
        for row in self.tree.get_children():
            self.tree.delete(row)

        if not DATA_ROOT.exists():
            return

        files = sorted(DATA_ROOT.glob("*/*.csv"), key=lambda p: p.stat().st_mtime, reverse=True)

        operators: dict[str, str] = {}
        for file_path in files:
            operator = file_path.parent.name
            if operator not in operators:
                operators[operator] = self.tree.insert(
                    "",
                    tk.END,
                    text=operator,
                    values=(operator, "", "", "", ""),
                    open=False,
                    tags=("operator",),
                )

            label = _extract_label_from_name(file_path.name)
            created = datetime.fromtimestamp(file_path.stat().st_mtime).strftime("%Y-%m-%d %H:%M:%S")

            samples = "-"
            try:
                with file_path.open("r", encoding="utf-8", newline="") as fp:
                    reader = csv.reader(fp)
                    header_found = False
                    data_count = 0
                    for row in reader:
                        if not row:
                            continue
                        if row[0].startswith("#"):
                            continue
                        if not header_found:
                            header_found = True
                            continue
                        data_count += 1
                    samples = str(data_count)
            except Exception:
                samples = "?"

            self.tree.insert(
                operators[operator],
                tk.END,
                values=(operator, label, created, samples, file_path.name),
                tags=("file",),
            )


def main() -> None:
    app = AcquisitionUI()
    app.mainloop()


if __name__ == "__main__":
    main()
