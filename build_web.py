import os
import subprocess
import shutil
from shutil import rmtree, copy2
from os.path import join, isdir, isfile
from sys import platform
from SCons.Script import COMMAND_LINE_TARGETS

Import("env")

clean_dirs = ["data"]

project_dir = os.getenv("PROJECT_DIR", os.getcwd())
if not project_dir:
    raise ValueError("PROJECT_DIR environment variable is not set")


def build_web_assets():
    web_src_dir = join(project_dir, "src", "web", "axe-os")
    dist_dir = join(web_src_dir, "dist", "axe-os")
    data_dir = join(project_dir, "data")

    npm = "npm.cmd" if platform == "win32" else "npm"

    if isdir(data_dir):
        rmtree(data_dir)

    subprocess.run([npm, "install"], cwd=web_src_dir, check=True)
    subprocess.run([npm, "run", "build"], cwd=web_src_dir, check=True)

    os.makedirs(data_dir, exist_ok=True)

    exclude_files = [
        "src/web/axe-os/node_modules/tempfile/node_modules/uuid/benchmark/benchmark-native.c"
    ]
    for file_path in exclude_files:
        if isfile(file_path):
            os.remove(file_path)

    for root, _, files in os.walk(dist_dir):
        for file_name in files:
            src_file = join(root, file_name)
            dest_file = join(data_dir, file_name)
            copy2(src_file, dest_file)

    # Pack both screensaver variants into SPIFFS; runtime selects by resolution.
    gif_dir = join(project_dir, "gif")
    screensaver_gifs = ["screen_saver_240x135.gif", "screen_saver_320x240.gif"]
    for gif_file in screensaver_gifs:
        src_gif = join(gif_dir, gif_file)
        if isfile(src_gif):
            copy2(src_gif, join(data_dir, gif_file))
            print(f"Copied screensaver GIF: {gif_file}")
        else:
            print(f"WARNING: screensaver GIF not found, skipped: {src_gif}")


def run_buildfs(env):
    build_env = os.environ.copy()
    build_env["NMAXE_SKIP_WEB_BUILD"] = "1"
    subprocess.run(
        [
            "pio",
            "run",
            "-e",
            env.subst("$PIOENV"),
            "-t",
            "buildfs",
        ],
        cwd=project_dir,
        env=build_env,
        check=True,
    )


def run_buildprog(env):
    build_env = os.environ.copy()
    build_env["NMAXE_SKIP_WEB_BUILD"] = "1"
    subprocess.run(
        [
            "pio",
            "run",
            "-e",
            env.subst("$PIOENV"),
        ],
        cwd=project_dir,
        env=build_env,
        check=True,
    )


def ensure_spiffs_before_upload(target=None, source=None, env=None, **kwargs):
    print("Prepare SPIFFS image before upload")
    env.AutodetectUploadPort()
    build_web_assets()
    run_buildprog(env)
    run_buildfs(env)
    esptool = join(project_dir, ".pio", "packages", "tool-esptoolpy", "esptool.py")
    upload_args = [
        env.subst("$PYTHONEXE"),
        esptool,
        "--chip", "esp32s3",
        "--port", env.subst("$UPLOAD_PORT"),
        "--baud", str(env.subst("$UPLOAD_SPEED")),
        "--before", "default_reset",
        "--after", "hard_reset",
        "write_flash",
        "-z",
        "--flash_mode", "keep",
        "--flash_freq", "80m",
        "--flash_size", "16MB",
        "0x0000", join(project_dir, "partitions", "bootloader.bin"),
        "0xf90000", join(project_dir, "partitions", "ota_data_initial.bin"),
        "0x8000", join(project_dir, ".pio", "build", env.subst("$PIOENV"), "partitions.bin"),
        "0x10000", join(project_dir, ".pio", "build", env.subst("$PIOENV"), "firmware.bin"),
        "0x410000", join(project_dir, ".pio", "build", env.subst("$PIOENV"), "spiffs.bin"),
    ]
    print("Run full flash command:", " ".join(upload_args))
    subprocess.run(upload_args, check=True)


if env.IsCleanTarget():
    for directory in clean_dirs:
        abs_path = join(project_dir, directory)
        if isdir(abs_path):
            print("rmdir: ", abs_path)
            shutil.rmtree(abs_path)
    Return()

if any(target == "upload" for target in COMMAND_LINE_TARGETS):
    env.AddPreAction("upload", ensure_spiffs_before_upload)
    env.Replace(UPLOADCMD="cmd /c echo Upload handled by build_web.py")
    Return()

if not any(target in ("buildfs", "uploadfs") for target in COMMAND_LINE_TARGETS):
    print("Skip web build: non-SPIFFS target", COMMAND_LINE_TARGETS)
    Return()

if os.getenv("NMAXE_SKIP_WEB_BUILD") == "1":
    print("Skip web build: data already prepared")
    Return()

build_web_assets()
