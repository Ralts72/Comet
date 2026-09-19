#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ $# -gt 0 ]]; then
    echo "Usage: $0"
    echo "Download demo HDRs in 1K, 2K, 4K, 8K order; verified local files are skipped."
    if [[ $# -eq 1 && ( "$1" == "--help" || "$1" == "-h" ) ]]; then
        exit 0
    fi
    exit 1
fi

if command -v sha256sum >/dev/null 2>&1; then
    HASH_COMMAND=(sha256sum)
elif command -v shasum >/dev/null 2>&1; then
    HASH_COMMAND=(shasum -a 256)
else
    echo "Error: sha256sum or shasum is required." >&2
    exit 1
fi

TEMP_DIR=""
trap 'if [[ -n "$TEMP_DIR" ]]; then rm -rf -- "$TEMP_DIR"; fi' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

verify() {
    local digest
    digest="$("${HASH_COMMAND[@]}" "$1")" || return 1
    [[ "${digest%% *}" == "$2" ]]
}

download() {
    local relative_path="$1" url="$2" sha256="$3"
    local destination="$ROOT_DIR/$relative_path"
    if [[ -f "$destination" ]] && verify "$destination" "$sha256"; then
        echo "Verified, skipping: $relative_path"
        return
    fi

    if ! command -v curl >/dev/null 2>&1; then
        echo "Error: curl is required to download $relative_path." >&2
        exit 1
    fi
    if [[ -z "$TEMP_DIR" ]]; then
        mkdir -p "$ROOT_DIR/demo/.comet"
        TEMP_DIR="$(mktemp -d "$ROOT_DIR/demo/.comet/download-assets.XXXXXX")"
    fi

    echo "Downloading: $relative_path"
    curl --fail --location --show-error --proto '=https' --proto-redir '=https' \
        --retry 3 --connect-timeout 15 --max-time 600 \
        --output "$TEMP_DIR/asset" "$url"
    if ! verify "$TEMP_DIR/asset" "$sha256"; then
        echo "Error: SHA-256 mismatch for $relative_path; existing file unchanged." >&2
        exit 1
    fi
    mkdir -p "$(dirname "$destination")"
    mv -f "$TEMP_DIR/asset" "$destination"
    echo "Installed: $relative_path"
}

# Keep each asset's URL and checksum pinned; record its source and license in README.md.
download "demo/assets/environments/small_hangar_01_1k.hdr" \
    "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/1k/small_hangar_01_1k.hdr" \
    "2ddc8b58715d325fde50f35bafe3cbe37434252bb51e1b744697f902bfebfb98"
download "demo/assets/environments/small_hangar_01_2k.hdr" \
    "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/2k/small_hangar_01_2k.hdr" \
    "cb7d97c4896891579ed92c306949acf8d1c18510905e8d5e5041716fbd12d036"
download "demo/assets/environments/small_hangar_01_4k.hdr" \
    "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/4k/small_hangar_01_4k.hdr" \
    "02f7cf6027d1b3310ca7625b71131e9b2750c4041f23a4e08e8cf36a5a4c352e"
download "demo/assets/environments/small_hangar_01_8k.hdr" \
    "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/8k/small_hangar_01_8k.hdr" \
    "50fd89b033db5026b6a999b82d29fcd53158664c2cb3d829d29d4b03143e97f7"
