#!/usr/bin/python3

from pathlib import Path

SEEDS = {
    "forward": "forward test@otherdomain.com",
    "reverse": "reverse SRS0=XjO9=2V=otherdomain.com=test@example.com",
}

DIR = Path(__file__).parent / "socketmap"

DIR.mkdir(exist_ok=True)
for seed_file, value in SEEDS.items():
    with open(DIR / seed_file, "w") as f:
        f.write(f"{len(value)}:{value},")
