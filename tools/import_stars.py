"""Extract the HYG v4.1 stars used by the renderer (no network at runtime)."""
import csv
import io
import math
from pathlib import Path
from urllib.request import urlopen

REVISION = "c7f7f883fe678cc7680169a50ccd7dcc49b060ce"
URL = f"https://raw.githubusercontent.com/astronexus/HYG-Database/{REVISION}/hyg/CURRENT/hygdata_v41.csv"
ROOT = Path(__file__).resolve().parents[1]


def main():
    with urlopen(URL, timeout=120) as response:
        rows = csv.DictReader(io.StringIO(response.read().decode("utf-8")))
    stars = []
    for row in rows:
        if row["id"] == "0" or not row["mag"]:
            continue  # Exclude the Sun.
        ra, dec, mag = (float(row[key]) for key in ("ra", "dec", "mag"))
        if mag > 7.5:
            continue
        bv = float(row["ci"]) if row["ci"] else 0.65
        if not all(math.isfinite(x) for x in (ra, dec, mag, bv)):
            raise ValueError(f"Non-finite star: {row['id']}")
        if not (0 <= ra < 24 and -90 <= dec <= 90):
            raise ValueError(f"Invalid coordinates: {row['id']}")
        stars.append((int(row["id"]), ra, dec, mag, bv))
    stars.sort()
    output = ROOT / "assets/stars/hyg_v41.csv"
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(("id", "ra_hours", "dec_degrees", "magnitude", "bv"))
        writer.writerows(stars)
    print(f"Wrote {len(stars)} stars to {output}")


if __name__ == "__main__":
    main()
