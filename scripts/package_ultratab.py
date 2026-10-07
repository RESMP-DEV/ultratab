"""Build a signed "Ultra Tab.app" for macOS, the way lapis.app is built and signed.

It uses the Qt that package_macos.py builds from pinned source, adds Qt SVG
(pinned too, in its own prefix so lapis.app does not pick it up), renders the
app icon from apps/ultratab/icon/icon.svg and signs inside out with the same
Developer ID identity (or LAPIS_SIGN_IDENTITY) and the hardened runtime.

    uv run --no-project python scripts/package_macos.py qt     # once, if not built
    uv run --no-project python scripts/package_ultratab.py app
    uv run --no-project python scripts/package_ultratab.py icon  # the .icns only

The app is left in build/release/ultratab/stage/; this script never installs it.
Notarizing is not part of this script yet.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import package_macos as lapis_package  # noqa: E402
from lapis import SetupError, ghostty_prefix  # noqa: E402
from package_macos import PackageError, capture, run  # noqa: E402

WORK = lapis_package.RELEASE / "ultratab"
STAGE = WORK / "stage"
APP_NAME = "Ultra Tab.app"
APP = STAGE / APP_NAME
BUILD = WORK / "build"
SVG_PREFIX = WORK / "qtsvg"
ICON_SOURCE = ROOT / "apps/ultratab/icon/icon.svg"
ICON = WORK / "AppIcon.icns"
ENTITLEMENTS = ROOT / "apps/ultratab/macos/ultratab.entitlements"
QTSVG_SHA256 = "d594337feca84c26fb67fe87b85e6a5c12fda404b611d905f9d138210c311876"
# Ultra Tab asks for no camera, contacts and the like, and uses no TLS.
UNUSED_PLUGINS = ("permissions", "tls", "networkinformation")


def qt_prefix(arguments):
    prefix = Path(arguments.qt).resolve() if arguments.qt else lapis_package.QT_PREFIX
    stamp = prefix / f".qtdeclarative-{lapis_package.QT_VERSION}"
    if not stamp.exists():
        raise PackageError(
            f"No release Qt at {prefix}: run package_macos.py qt, or pass --qt PREFIX"
        )
    return prefix


def build_qtsvg(qt):
    """Qt SVG from its pinned source, staged beside (not into) the release Qt."""
    stamp = SVG_PREFIX / f".qtsvg-{lapis_package.QT_VERSION}"
    if stamp.exists():
        print(f"qtsvg {lapis_package.QT_VERSION} already built", flush=True)
        return
    source = lapis_package.fetch("qtsvg", QTSVG_SHA256)
    build = WORK / "qtsvg-build"
    shutil.rmtree(build, ignore_errors=True)
    arguments = lapis_package.common_cmake_arguments() + [
        f"-DCMAKE_INSTALL_PREFIX={lapis_package.QT_RUNTIME_PREFIX}",
        f"-DCMAKE_STAGING_PREFIX={SVG_PREFIX}",
        f"-DCMAKE_PREFIX_PATH={qt}",
        "-DQT_BUILD_EXAMPLES=OFF",
        "-DQT_BUILD_TESTS=OFF",
        "-DQT_GENERATE_SBOM=ON",
    ]
    run(["cmake", "-S", source, "-B", build] + arguments)
    run(
        [
            "nice",
            "-n",
            "10",
            "cmake",
            "--build",
            build,
            "--parallel",
            lapis_package.JOBS,
        ]
    )
    run(["cmake", "--install", build])
    stamp.touch()


def build_icon():
    """AppIcon.icns from the SVG, every size rendered from vectors."""
    renderer = shutil.which("rsvg-convert")
    if renderer is None:
        raise PackageError("SVG renderer is missing: brew install librsvg")
    if shutil.which("iconutil") is None:
        raise PackageError("Generating the Mac icon requires macOS iconutil")
    WORK.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="icon-", dir=WORK) as temporary:
        iconset = Path(temporary) / "AppIcon.iconset"
        iconset.mkdir()
        for points in (16, 32, 128, 256, 512):
            for factor, suffix in ((1, ""), (2, "@2x")):
                pixels = points * factor
                target = iconset / f"icon_{points}x{points}{suffix}.png"
                run(
                    [
                        renderer,
                        "--width",
                        pixels,
                        "--height",
                        pixels,
                        "--output",
                        target,
                        ICON_SOURCE,
                    ]
                )
        run(["iconutil", "--convert", "icns", "--output", ICON, iconset])
    return ICON


def build_app(qt):
    try:
        ghostty = ghostty_prefix()
    except SetupError as error:
        raise PackageError(str(error)) from error
    run(
        ["cmake", "-S", ROOT, "-B", BUILD]
        + lapis_package.common_cmake_arguments()
        + [
            f"-DCMAKE_PREFIX_PATH={qt};{SVG_PREFIX}",
            f"-DQT_ADDITIONAL_PACKAGES_PREFIX_PATH={SVG_PREFIX}",
            "-DLAPIS_BUILD_DESKTOP=OFF",
            "-DLAPIS_BUILD_ULTRATAB=ON",
            "-DBUILD_TESTING=OFF",
            f"-DLAPIS_GHOSTTY_PREFIX={ghostty}",
        ]
    )
    run(
        ["cmake", "--build", BUILD, "--target", "lapis_ultratab"]
        + ["--parallel", str(lapis_package.JOBS)]
    )
    return BUILD / "apps" / "ultratab" / APP_NAME


def deploy(qt, built, icon):
    """Copy the build into a bundle that carries Qt, Qt SVG and the notices."""
    shutil.rmtree(STAGE, ignore_errors=True)
    STAGE.mkdir(parents=True)
    run(["ditto", built, APP])
    contents = APP / "Contents"
    executable = contents / "MacOS" / "Ultra Tab"
    lapis_package.scrub_build_paths(executable)
    resources = contents / "Resources"
    resources.mkdir(exist_ok=True)
    shutil.copy2(icon, resources / "AppIcon.icns")
    run(
        [qt / "bin" / "macdeployqt", APP]
        + [f"-qmldir={ROOT / 'apps/ultratab/qml'}", "-always-overwrite"]
        + [f"-libpath={SVG_PREFIX / 'lib'}"]
    )
    svg = contents / "Frameworks" / "QtSvg.framework"
    if not svg.exists():
        run(["ditto", SVG_PREFIX / "lib" / "QtSvg.framework", svg])
    for unused in UNUSED_PLUGINS:
        shutil.rmtree(contents / "PlugIns" / unused, ignore_errors=True)
    notices = resources / "Notices"
    notices.mkdir(exist_ok=True)
    for name in ("lapis-LICENSE.txt", "Qt-NOTICES.txt", "Ghostty-NOTICES.txt"):
        shutil.copy2(lapis_package.NOTICES[name], notices / name)
    # Build-machine rpaths go; the bundle's own Frameworks folder stays.
    for path in lapis_package.macho_files(APP):
        for rpath in capture(["otool", "-l", path]).split("cmd LC_RPATH")[1:]:
            target = rpath.split("path ", 1)[1].split()[0]
            if target.startswith("/"):
                run(["install_name_tool", "-delete_rpath", target, path])
        run(["strip", "-S", "-x", path], stderr=subprocess.DEVNULL)


def check_dependencies():
    """Nothing in the bundle may load a library from outside it or the system."""
    problems = []
    for path in lapis_package.macho_files(APP):
        for line in capture(["otool", "-L", path]).splitlines()[1:]:
            library = line.strip().split(" (")[0]
            if not library.startswith(lapis_package.ALLOWED_LIBRARY_PREFIXES):
                problems.append(f"{path.relative_to(APP)} loads {library}")
    if problems:
        raise PackageError("Libraries from outside the bundle:\n" + "\n".join(problems))


def sign(identity):
    """Sign inside out with the hardened runtime and a secure timestamp."""
    base = ["codesign", "--force", "--timestamp", "--options", "runtime"]
    contents = APP / "Contents"
    frameworks = sorted((contents / "Frameworks").glob("*.framework"))
    loose = [
        path
        for path in lapis_package.macho_files(APP)
        if not any(framework in path.parents for framework in frameworks)
        and path.parent != contents / "MacOS"
    ]
    for path in loose:
        run(base + ["--sign", identity, path])
    for framework in frameworks:
        run(base + ["--sign", identity, framework])
    run(base + ["--entitlements", ENTITLEMENTS, "--sign", identity, APP])
    run(["codesign", "--verify", "--deep", "--strict", "--verbose=2", APP])


def command_icon(_arguments):
    print(f"Wrote {build_icon()}", flush=True)


def command_app(arguments):
    if sys.platform != "darwin":
        raise PackageError("Ultra Tab is packaged on macOS")
    qt = qt_prefix(arguments)
    identity = lapis_package.signing_identity()
    build_qtsvg(qt)
    icon = build_icon()
    built = build_app(qt)
    deploy(qt, built, icon)
    check_dependencies()
    sign(identity)
    print(f"Signed {APP}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    app = commands.add_parser("app", help="Build, bundle and sign Ultra Tab.app")
    app.add_argument(
        "--qt",
        default=os.environ.get("ULTRATAB_QT_PREFIX"),
        help="release Qt prefix (default: package_macos.py's build/release/qt)",
    )
    commands.add_parser("icon", help="Render AppIcon.icns from the icon SVG")
    arguments = parser.parse_args()
    handlers = {"app": command_app, "icon": command_icon}
    try:
        handlers[arguments.command](arguments)
    except (PackageError, subprocess.CalledProcessError) as error:
        print(f"package: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
