#!/usr/bin/env python3
"""Orbit Store TV app - write sce_sys/param.sfo from sce_sys/param.json.

The PS4 package format expects a binary SFO, not the PS5-style param.json.
This maps the shared param.json fields onto the key set every working
homebrew param.sfo carries (OpenOrbis PkgEditor shape, gd category).
"""
import json
import re
import struct
import sys
from datetime import datetime

FMT_UTF8 = 0x0204   # NUL-terminated string
FMT_UINT32 = 0x0404


def utf8(value):
    data = value.encode("utf-8") + b"\0"
    return FMT_UTF8, data


def uint32(value):
    return FMT_UINT32, struct.pack("<I", value)


def build_entries(param):
    localized = param.get("localizedParameters", {})
    language = localized.get("defaultLanguage", "en-US")
    title = localized.get(language, {}).get("titleName", "")
    title_id = param["titleId"]
    content_id = param["contentId"]
    version = param.get("masterVersion", "01.00")
    app_version = param.get("appVersion", version)
    category = param.get("category", "gd")
    if category not in ("gd", "gde"):
        raise ValueError("category must be gd or gde")
    for name, value in (("masterVersion", version), ("appVersion", app_version)):
        if not isinstance(value, str) or not re.fullmatch(r"[0-9]{2}\.[0-9]{2}", value):
            raise ValueError(f"{name} must use the PS4 NN.NN version format")
    pubtools = param.get("pubtools", {})

    try:
        created = datetime.strptime(
            pubtools.get("creationDate", ""), "%Y-%m-%d %H:%M:%S")
        c_date = created.strftime("%Y%m%d")
    except ValueError:
        c_date = "19700101"
    sdk_ver = int(param.get("sdkVersion", "0"), 16)
    tool_version = pubtools.get("toolVersion", "0.0")
    try:
        major, minor = (int(part) for part in tool_version.split(".")[:2])
    except ValueError:
        major = minor = 0

    parental = param.get("ageLevel", {}).get("default", 0)
    if not isinstance(parental, int) or parental < 1:
        parental = 1

    entries = {
        "APP_TYPE": uint32(1),
        "APP_VER": utf8(app_version),
        "ATTRIBUTE": uint32(int(param.get("attribute", 0))),
        "CATEGORY": utf8(category),
        "CONTENT_ID": utf8(content_id),
        "DOWNLOAD_DATA_SIZE": uint32(int(param.get("downloadDataSize", 0))),
        "FORMAT": utf8("obs"),
        "PARENTAL_LEVEL": uint32(parental),
        "PUBTOOLINFO": utf8(f"c_date={c_date},sdk_ver={sdk_ver:08x},0"),
        "PUBTOOLVER": uint32((major << 24) | (minor << 16)),
        "SERVICE_ID_ADDCONT_EXIST": uint32(0),
        "SYSTEM_VER": uint32(int(param.get("requiredSystemSoftwareVersion", "0"), 16)),
        "TITLE": utf8(title),
        "TITLE_ID": utf8(title_id),
        "VERSION": utf8(version),
    }
    return entries


def write_sfo(entries, stream):
    keys = sorted(entries)
    header_size = 0x14
    index_size = 16 * len(keys)
    key_start = header_size + index_size
    data_start = key_start
    key_blobs = []
    for key in keys:
        key_blobs.append(key.encode("ascii") + b"\0")
        data_start += len(key_blobs[-1])
    # The PS4 parser requires the value table (including integer values) to
    # start on a four-byte boundary, independently of each value's padding.
    key_padding = (-data_start) % 4
    data_start += key_padding

    index = []
    data_blobs = []
    key_offset = 0
    data_offset = 0
    for key, blob in zip(keys, key_blobs):
        fmt, data = entries[key]
        # The installer compares CONTENT_ID against the PKG's 48-byte field.
        # Reserving only the string's rounded length lets the following entry
        # bleed into that comparison, producing CONTENT_ID_DISAGREE.
        minimum_capacity = {"TITLE": 128, "CONTENT_ID": 48}.get(key, 0)
        max_len = max(len(data), minimum_capacity)
        # Strings are stored NUL-terminated and 4-byte aligned.
        max_len = (max_len + 3) & ~3
        index.append((key_offset, fmt, len(data), max_len, data_offset))
        data_blobs.append(data.ljust(max_len, b"\0"))
        key_offset += len(blob)
        data_offset += max_len

    stream.write(struct.pack("<5I", 0x46535000, 0x00000101, key_start,
                             data_start, len(keys)))
    for key_offset, fmt, data_len, max_len, data_offset in index:
        stream.write(struct.pack("<HHIII", key_offset, fmt, data_len,
                                 max_len, data_offset))
    for blob in key_blobs:
        stream.write(blob)
    stream.write(b"\0" * key_padding)
    for blob in data_blobs:
        stream.write(blob)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: make_sfo.py param.json param.sfo")
    with open(sys.argv[1], encoding="utf-8") as source:
        param = json.load(source)
    entries = build_entries(param)
    with open(sys.argv[2], "wb") as stream:
        write_sfo(entries, stream)


if __name__ == "__main__":
    main()
