#!/usr/bin/env python3
# Copyright (c) 2026 The Brave Authors. All rights reserved.
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at https://mozilla.org/MPL/2.0/.
"""Builds indexed offline geo data for the signed component payload."""

import argparse
import csv
import gzip
import shutil
import sys
import zipfile
from pathlib import Path


def read_country_languages(country_info: Path) -> dict[str, str]:
    languages: dict[str, str] = {}
    with country_info.open(encoding="utf-8", newline="") as source:
        for row in csv.reader(source, delimiter="\t"):
            if not row or row[0].startswith("#") or len(row) < 16:
                continue
            country = row[0].upper()
            primary = row[15].split(",", 1)[0].strip()
            if len(country) != 2 or not primary:
                continue
            locale = primary if "-" in primary else f"{primary}-{country}"
            languages[country] = f"{locale},{primary}"
    return languages


def read_city_timezones(cities_zip: Path) -> list[tuple[str, str, str, str]]:
    with zipfile.ZipFile(cities_zip) as archive:
        city_file = next(name for name in archive.namelist()
                         if name.endswith("cities500.txt"))
        with archive.open(city_file) as raw_source:
            source = (line.decode("utf-8") for line in raw_source)
            rows = []
            for row in csv.reader(source, delimiter="\t"):
                if len(row) < 18 or not row[17]:
                    continue
                rows.append((row[8].upper(), row[4], row[5], row[17]))
            return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dbip-mmdb-gz", type=Path, required=True)
    parser.add_argument("--cities500-zip", type=Path, required=True)
    parser.add_argument("--country-info", type=Path, required=True)
    parser.add_argument("--dbip-license", type=Path, required=True)
    parser.add_argument("--geonames-license", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    with gzip.open(
            args.dbip_mmdb_gz,
            "rb") as source, (args.output_dir /
                              "dbip-city-lite.mmdb").open("wb") as destination:
        shutil.copyfileobj(source, destination)

    with (args.output_dir / "geonames-city-timezones.tsv").open(
            "w", encoding="utf-8", newline="") as output:
        for row in read_city_timezones(args.cities500_zip):
            output.write("\t".join(row) + "\n")

    with (args.output_dir / "geonames-country-languages.tsv").open(
            "w", encoding="utf-8", newline="") as output:
        for country, languages in sorted(
                read_country_languages(args.country_info).items()):
            output.write(f"{country}\t{languages}\n")

    licenses = args.output_dir / "LICENSES"
    licenses.mkdir(exist_ok=True)
    shutil.copyfile(args.dbip_license, licenses / "DB-IP-Lite-CC-BY-4.0.txt")
    shutil.copyfile(args.geonames_license, licenses / "GeoNames-CC-BY-4.0.txt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
