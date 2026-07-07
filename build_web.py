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
    # Legacy web asset pipeline kept for the future full-flash flow.
    # The current BSP-first skeleton does not restore `src/web/axe-os` yet, so
    # normal `pio run -t upload` must not assume this path already exists.
    web_src_dir = join(project_dir, "src", "web", "axe-os")
    dist_dir = join(web_src_dir, "dist", "axe-os")
    data_dir = join(project_dir, "data")

    npm = "npm.cmd" if platform == "win32" else "npm"

    if not isdir(web_src_dir):
        raise RuntimeError(
            "Legacy web asset path is missing: {}. Restore the new web/SPiffs "
            "pipeline before re-enabling full-flash upload.".format(web_src_dir)
        )

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


def prepare_spiffs_payload():
    legacy_web_dir = join(project_dir, "src", "web", "axe-os")
    data_dir = join(project_dir, "data")

    if isdir(legacy_web_dir):
        build_web_assets()
        return

    # Temporary skeleton fallback:
    # Keep the legacy full-flash upload workflow alive even before the new web
    # pipeline is restored. We only guarantee that `data/` exists so PlatformIO
    # can still generate `spiffs.bin` and the custom upload command can flash
    # all bins (bootloader/ota/partitions/firmware/spiffs) in one shot.
    #
    # TODO(agent): once the rebuilt web UI lands, replace this fallback with the
    # real web asset preparation path and remove the empty-SPIFFS bootstrap mode.
    os.makedirs(data_dir, exist_ok=True)
    print("Legacy web tree missing; generate SPIFFS from current data/ contents")


def ensure_spiffs_before_upload(target=None, source=None, env=None, **kwargs):
    print("Prepare SPIFFS image before upload")
    env.AutodetectUploadPort()
    prepare_spiffs_payload()
    run_buildfs(env)


if env.IsCleanTarget():
    for directory in clean_dirs:
        abs_path = join(project_dir, directory)
        if isdir(abs_path):
            print("rmdir: ", abs_path)
            shutil.rmtree(abs_path)
    Return()

if any(target == "upload" for target in COMMAND_LINE_TARGETS):
    # Current BSP-first upload mode skips SPIFFS on purpose.
    # `platformio.ini` still uses a custom full-flash command, but only for:
    # bootloader + ota_data + partitions + firmware.
    #
    # The SPIFFS preparation helpers above are intentionally kept in this file
    # for later restoration, but upload does not hook them right now.
    #
    # TODO(agent): when the new web/SPiffs pipeline returns, add the upload
    # pre-action back here and extend upload_command to flash spiffs.bin again.
    Return()

if not any(target in ("buildfs", "uploadfs") for target in COMMAND_LINE_TARGETS):
    print("Skip web build: non-SPIFFS target", COMMAND_LINE_TARGETS)
    Return()

if os.getenv("NMAXE_SKIP_WEB_BUILD") == "1":
    print("Skip web build: data already prepared")
    Return()

build_web_assets()
