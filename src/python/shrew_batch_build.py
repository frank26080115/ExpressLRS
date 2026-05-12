BUILD_TARGETS = [
    ["Unified_ShrewForXR2_2400_RX_via_UART", None],
    ["Unified_ShrewForRP4TD_2400_RX_via_UART", None],
    ["Unified_ShrewForRP4TD_VESC_2400_RX_via_UART", None],
    ["Unified_ShrewForER4_ESP8285_2400_RX_via_WIFI", None],
    ["Unified_ShrewForER4_VESC_ESP8285_2400_RX_via_WIFI", None],
    ["Unified_ESP32C3_LR1121_RX_via_WIFI", ["xr1", "xr2", "xr3"]],
    ["Unified_ESP32_LR1121_RX_via_WIFI", ["xr4"]],
    ["Unified_ESP8285_2400_RX_via_WIFI", ["er3", "er4", "er5", "er5c-i", "er5-v2", "rp1", "rp2"]],
    ["Unified_ESP32_2400_RX_via_WIFI", ["er6", "er8", "rp4", "rp4m"]],
    ["Unified_ESP32_2400_TX_via_WIFI", ["mt12", "boxer", "zorro", "pocket", "tx16s", "tx12", "ranger", "ranger-micro", "ranger-nano"]],
    ["Unified_ESP32_LR1121_TX_via_WIFI", ["nomad", "gx12", "tx15"]],
    #["Unified_ESP32S3_2400_TX", ["t12-1w"]]
]

import argparse
import gzip
import json
import os
import re
import shlex
import shutil
import subprocess
from datetime import datetime
from pathlib import Path

from UnifiedConfiguration import doConfiguration, findFirmwareEnd


SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
HTML_DIR = PROJECT_DIR / "html"
BUILD_DIR = PROJECT_DIR / ".pio" / "build"
RESULTS_DIR = PROJECT_DIR / "batch_build_results"
USER_DEFINES = PROJECT_DIR / "user_defines.txt"
TARGETS_JSON = PROJECT_DIR / "hardware" / "targets.json"
BATCH_TIMESTAMP = datetime.now().strftime("%Y%m%d%H%M")
RUN_RESULTS_DIR = RESULTS_DIR / BATCH_TIMESTAMP


def fail(message):
    raise SystemExit(f"ERROR: {message}")


def platformio_executable():
    pio = shutil.which("pio")
    if pio:
        return pio

    home = Path.home()
    candidates = [
        home / ".platformio" / "penv" / "Scripts" / "pio.exe",
        home / ".platformio" / "penv" / "bin" / "pio",
    ]

    for candidate in candidates:
        if candidate.exists():
            return str(candidate)

    fail("could not find PlatformIO executable; install it or add `pio` to PATH")


def parse_args():
    parser = argparse.ArgumentParser(description="Build Shrew ExpressLRS firmware batch artifacts.")
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Run `pio run -t clean` for each target before building it.",
    )
    parser.add_argument(
        "--build-web",
        action="store_true",
        help="Run npm run build:all in the automatically detected html directory before firmware builds.",
    )
    parser.add_argument(
        "--build-web-wsl",
        action="store_true",
        help="Run the optional web build through WSL. Implies --build-web.",
    )
    return parser.parse_args()


def validate_user_defines():
    if not USER_DEFINES.exists():
        return

    for line_number, line in enumerate(USER_DEFINES.read_text(encoding="utf-8").splitlines(), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue

        if "-DMY_BINDING_PHRASE" in stripped:
            fail(f"{USER_DEFINES.name}:{line_number}: -DMY_BINDING_PHRASE must not be defined")

        if re.match(r"!?\s*-DDEBUG_", stripped):
            fail(f"{USER_DEFINES.name}:{line_number}: debug define must be disabled")


def result_stem(target, hardware_name=None):
    name = target.removeprefix("Unified_")
    name = re.sub(r"_via_(UART|WIFI)$", "", name)

    parts = name.split("_")
    if "TX" in parts:
        stem = name.strip("_")
        return f"{stem}_{hardware_name}" if hardware_name else stem

    parts = [part for part in parts if part != "RX" and not part.isdigit()]
    stem = "_".join(parts).strip("_")
    return f"{stem}_{hardware_name}" if hardware_name else stem


def find_firmware(target):
    target_dir = BUILD_DIR / target
    for filename in ("firmware.bin.gz", "firmware.bin"):
        firmware = target_dir / filename
        if firmware.exists():
            return firmware
    fail(f"no firmware.bin or firmware.bin.gz produced for {target}")


def target_firmware_name(target):
    return re.sub(r"_via_(UART|WIFI)$", "", target)


def find_hardware_config(target, hardware_name):
    firmware_name = target_firmware_name(target)

    with TARGETS_JSON.open(encoding="utf-8") as f:
        targets = json.load(f)

    def visit(node, path):
        if not isinstance(node, dict):
            return None

        if path and path[-1].casefold() == hardware_name.casefold() and node.get("firmware") == firmware_name:
            return ".".join(path)

        for key, value in node.items():
            found = visit(value, path + [key])
            if found is not None:
                return found

        return None

    config = visit(targets, [])
    if config is None:
        fail(f"no hardware target named {hardware_name!r} found for {firmware_name}")

    return config


def preserve_defines(firmware_file):
    end = findFirmwareEnd(firmware_file)
    firmware_file.seek(end + 128 + 16, 0)
    defines = firmware_file.read(512).split(b"\0", 1)[0]
    return defines.decode(errors="ignore")


def configure_firmware(firmware, target, hardware_name):
    config = find_hardware_config(target, hardware_name)

    with firmware.open("r+b") as firmware_file:
        defines = preserve_defines(firmware_file)
        doConfiguration(firmware_file, defines, config, target, hardware_name, None)


def build_target(target, built_targets, clean):
    if target in built_targets:
        return

    print(f"Building {target}...")
    target_dir = BUILD_DIR / target
    if target_dir.is_file():
        target_dir.unlink()
    target_dir.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["ELRS_BATCH_BUILD"] = "1"
    pio = platformio_executable()
    if clean:
        print(f"Cleaning {target}...")
        subprocess.run([pio, "run", "-e", target, "-t", "clean"], cwd=PROJECT_DIR, env=env, stdin=subprocess.DEVNULL, check=True)
    subprocess.run([pio, "run", "-e", target], cwd=PROJECT_DIR, env=env, stdin=subprocess.DEVNULL, check=True)
    built_targets.add(target)


def find_html_dir():
    if not HTML_DIR.is_dir():
        fail(f"html directory not found at {HTML_DIR}")
    if not (HTML_DIR / "package.json").is_file():
        fail(f"package.json not found in html directory at {HTML_DIR}")
    return HTML_DIR


def wsl_path(path):
    resolved = path.resolve()
    drive = resolved.drive.rstrip(":")
    if drive:
        rest = resolved.relative_to(resolved.anchor).as_posix()
        return f"/mnt/{drive.lower()}/{rest}"

    try:
        return subprocess.check_output(["wsl", "wslpath", "-a", str(resolved)], text=True).strip()
    except (FileNotFoundError, subprocess.CalledProcessError) as exc:
        fail(f"unable to convert html path for WSL: {exc}")


def build_web_headers(use_wsl):
    html_dir = find_html_dir()
    print(f"Building web UI headers in {html_dir}" + (" via WSL" if use_wsl else ""))

    if use_wsl:
        command = f"cd {shlex.quote(wsl_path(html_dir))} && npm run build:all"
        subprocess.run(["wsl", "bash", "-lc", command], check=True)
        return

    npm = "npm.cmd" if os.name == "nt" else "npm"
    subprocess.run([npm, "run", "build:all"], cwd=html_dir, check=True)


def hardware_names(hardware):
    if hardware is None:
        return [None]
    if isinstance(hardware, str):
        return [hardware]
    return hardware


def git_output(args):
    return subprocess.check_output(["git", *args], cwd=PROJECT_DIR, text=True).strip()


def git_tag_hash(tag):
    tag_object = git_output(["rev-parse", tag])
    commit_object = git_output(["rev-parse", f"{tag}^{{commit}}"])
    return tag_object if tag_object != commit_object else commit_object


def write_readme():
    commit = git_output(["rev-parse", "HEAD"])
    tags = [tag for tag in git_output(["tag", "--points-at", "HEAD"]).splitlines() if tag]

    lines = [
        "# Batch Build Results",
        "",
        f"Build timestamp: {BATCH_TIMESTAMP}",
        f"Git commit: {commit}",
    ]

    if tags:
        lines.append("")
        lines.append("Tags attached to this commit:")
        for tag in tags:
            lines.append(f"- {tag}: {git_tag_hash(tag)}")
    else:
        lines.append("Tags attached to this commit: none")

    (RUN_RESULTS_DIR / "readme.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def copy_target_result(target, hardware_name):
    RUN_RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    firmware = find_firmware(target)
    extension = ".bin.gz" if firmware.name.endswith(".bin.gz") else ".bin"
    output = RUN_RESULTS_DIR / f"fw_{result_stem(target, hardware_name)}_{BATCH_TIMESTAMP}{extension}"

    print(f"Preparing result for {target}" + (f" ({hardware_name})" if hardware_name else ""))
    print(f"  firmware: {firmware}")
    print(f"  output:   {output}")

    if hardware_name and extension == ".bin.gz":
        uncompressed_firmware = BUILD_DIR / target / "firmware.bin"
        uncompressed_output = output.with_suffix("")
        print(f"  uncompressed source: {uncompressed_firmware}")
        print(f"  temporary output:    {uncompressed_output}")
        if uncompressed_firmware.exists():
            print("  copying uncompressed firmware before configuration")
            shutil.copy2(uncompressed_firmware, uncompressed_output)
        else:
            print("  decompressing firmware before configuration")
            with gzip.open(firmware, "rb") as f_in, uncompressed_output.open("wb") as f_out:
                shutil.copyfileobj(f_in, f_out)

        print("  embedding hardware configuration")
        configure_firmware(uncompressed_output, target, hardware_name)
        print("  recompressing configured firmware")
        with uncompressed_output.open("rb") as f_in, gzip.open(output, "wb") as f_out:
            shutil.copyfileobj(f_in, f_out)
        uncompressed_output.unlink()
        print(f"Saved {output}")
        return

    print("  copying firmware")
    shutil.copy2(firmware, output)
    if hardware_name:
        print("  embedding hardware configuration")
        configure_firmware(output, target, hardware_name)
    print(f"Saved {output}")


def main():
    args = parse_args()
    os.chdir(PROJECT_DIR)
    validate_user_defines()
    if args.build_web or args.build_web_wsl:
        build_web_headers(args.build_web_wsl)
    RUN_RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    write_readme()
    built_targets = set()

    for target, hardware in BUILD_TARGETS:
        build_target(target, built_targets, args.clean)
        for hardware_name in hardware_names(hardware):
            copy_target_result(target, hardware_name)

    print(f"Batch build complete. Results are in {RUN_RESULTS_DIR}")


if __name__ == "__main__":
    main()
