"""Run Ultra Tab for iPhone's tests in a headless iOS Simulator against real services.

Starts, all disposable and on this Mac:
- two session services running a shell that echoes each line it reads
  ("kernels" and "docs"), each held by a Mac-side client the way the lapis
  window holds it (attached, 100 columns), which records what it sees,
- registry entries for an agent with a pending request ("approve"), one whose
  service is not running ("parked") and one at work ("trainer"),
- the published deck beside them: agent_state.json and ultratab_cards.json
  with every block type (text, list, table, diagram, link),
- the gateway (apps/remote/lapis_remote.py) on 127.0.0.1 with --allow-local.

Then it runs the UltraTabUITests (deal the deck, swipe, dictate with a
scripted transcriber, a refused send, swipe poses) and the UltraTabTests
unit bundle on a headless simulator (no Simulator window), checks that the
Mac-side clients received exactly the phone's answers and were never
replaced or resized, and exports the screenshots to
build/ios-ultratab/screens/<time>/.

The apps are compiled directly (swiftc, actool, codesign) as
scripts/check_ios_remote.py compiles lapis's, and run with
`xcodebuild test-without-building`.

    uv run --no-project python scripts/check_ultratab_ios.py [--unit] [--only testDeal]
"""

import argparse
import json
import os
import plistlib
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "apps" / "remote"))
import check_ios_remote as lapis_ios  # noqa: E402
import lapis_remote  # noqa: E402
from check_ios_remote import (  # noqa: E402
    DEVICE,
    SERVICE,
    TARGET,
    bundle_xctest_frameworks,
    sign,
    simulator,
    tool,
)

IOS = ROOT / "apps" / "ios"
APP_SOURCES = IOS / "UltraTab"
UI_SOURCES = IOS / "UltraTabUITests"
UNIT_SOURCES = IOS / "UltraTabTests"
# The app's logic, compiled into the unit bundle beside its tests.
LOGIC = ("Cards.swift", "Deck.swift", "DeckGateway.swift")
BUILD = ROOT / "build" / "ios-ultratab"
PRODUCTS = BUILD / "sim"
PORT = 7351
APP_ID = "dev.lapis.ultratab"
RUNNER_ID = "dev.lapis.ultratab.uitests.xctrunner"
ECHO = 'printf "ready\\n"; while read line; do printf "got:%s\\n" "$line"; done'

KERNELS_REPLY = "Run the full benchmark sweep and post the table"
DOCS_ANNOTATION = "Make the arrows gold please"

DIAGRAM = (
    '<svg viewBox="0 0 330 120" xmlns="http://www.w3.org/2000/svg" '
    'font-family="Menlo" font-size="12">'
    '<defs><marker id="a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" '
    'markerHeight="7" orient="auto"><path d="M0 0L10 5L0 10z" fill="#e8b931"/>'
    "</marker></defs>"
    '<g fill="#1c1f29" stroke="#3a3f4f">'
    '<rect x="5" y="40" width="80" height="40" rx="8"/>'
    '<rect x="125" y="40" width="80" height="40" rx="8"/>'
    '<rect x="245" y="40" width="80" height="40" rx="8"/></g>'
    '<g stroke="#e8b931" stroke-width="2" marker-end="url(#a)">'
    '<path d="M87 60H121"/><path d="M207 60H241"/></g>'
    '<g text-anchor="middle"><text x="45" y="64">iPhone</text>'
    '<text x="165" y="64">gateway</text><text x="285" y="64">session</text></g>'
    '<text x="165" y="22" text-anchor="middle" fill="#8a93a6">join, paste, Return</text>'
    "</svg>"
)


def build_app(sdk):
    """Ultra Tab.app for the simulator, from the same sources and Info.plist."""
    app = PRODUCTS / "UltraTab.app"
    shutil.rmtree(app, ignore_errors=True)
    app.mkdir(parents=True)
    tool(
        "xcrun",
        "swiftc",
        "-target",
        TARGET,
        "-sdk",
        sdk,
        "-parse-as-library",
        "-Onone",
        "-g",
        "-module-name",
        "UltraTab",
        *sorted(str(path) for path in APP_SOURCES.glob("*.swift")),
        "-o",
        str(app / "UltraTab"),
    )
    partial = PRODUCTS / "assets.plist"
    tool(
        "xcrun",
        "actool",
        str(APP_SOURCES / "Assets.xcassets"),
        "--compile",
        str(app),
        "--platform",
        "iphonesimulator",
        "--minimum-deployment-target",
        "17.0",
        "--app-icon",
        "AppIcon",
        "--target-device",
        "iphone",
        "--output-partial-info-plist",
        str(partial),
    )
    info = app_info("", partial)
    info.update(
        {
            "CFBundleSupportedPlatforms": ["iPhoneSimulator"],
            "DTPlatformName": "iphonesimulator",
        }
    )
    (app / "Info.plist").write_bytes(plistlib.dumps(info))
    sign(app)
    return app


def app_info(host, partial):
    """The app's Info.plist with Xcode's build settings filled in."""
    values = {
        "DEVELOPMENT_LANGUAGE": "en",
        "EXECUTABLE_NAME": "UltraTab",
        "PRODUCT_BUNDLE_IDENTIFIER": APP_ID,
        "PRODUCT_NAME": "UltraTab",
        "LAPIS_DEFAULT_HOST": host,
    }
    info = plistlib.loads((APP_SOURCES / "Info.plist").read_bytes())
    for key, value in info.items():
        if isinstance(value, str) and value.startswith("$(") and value.endswith(")"):
            info[key] = values[value[2:-1]]
    info.update(plistlib.loads(partial.read_bytes()))
    info.update(
        {"MinimumOSVersion": "17.0", "UIDeviceFamily": [1], "LSRequiresIPhoneOS": True}
    )
    return info


def test_bundle(sdk, developer, runner, name, sources):
    bundle = runner / "PlugIns" / f"{name}.xctest"
    bundle.mkdir(parents=True)
    tool(
        "xcrun",
        "swiftc",
        "-target",
        TARGET,
        "-sdk",
        sdk,
        "-emit-library",
        "-Xlinker",
        "-bundle",
        "-module-name",
        name,
        "-Onone",
        "-g",
        "-F",
        str(developer / "Library/Frameworks"),
        "-I",
        str(developer / "usr/lib"),
        "-L",
        str(developer / "usr/lib"),
        "-framework",
        "XCTest",
        "-Xlinker",
        "-rpath",
        "-Xlinker",
        "@executable_path/Frameworks",
        "-Xlinker",
        "-rpath",
        "-Xlinker",
        "@loader_path/../../Frameworks",
        *sources,
        "-o",
        str(bundle / name),
    )
    (bundle / "Info.plist").write_bytes(
        plistlib.dumps(
            {
                "CFBundleExecutable": name,
                "CFBundleIdentifier": f"dev.lapis.ultratab.{name.lower()}",
                "CFBundleName": name,
                "CFBundlePackageType": "BNDL",
                "CFBundleShortVersionString": "1.0",
                "CFBundleVersion": "1",
                "CFBundleSupportedPlatforms": ["iPhoneSimulator"],
                "MinimumOSVersion": "17.0",
            }
        )
    )
    sign(bundle)


def build_runner(sdk, platform):
    """XCTRunner hosting the UI and unit bundles, as Xcode assembles it."""
    developer = Path(platform) / "Developer"
    runner = PRODUCTS / "UltraTabUITests-Runner.app"
    shutil.rmtree(runner, ignore_errors=True)
    shutil.copytree(
        developer / "Library/Xcode/Agents/XCTRunner.app", runner, symlinks=True
    )
    frameworks = runner / "Frameworks"
    frameworks.mkdir()
    bundle_xctest_frameworks(developer, frameworks)
    (runner / "XCTRunner").rename(runner / "UltraTabUITests-Runner")
    info = plistlib.loads((runner / "Info.plist").read_bytes())
    info.update(
        {
            "CFBundleExecutable": "UltraTabUITests-Runner",
            "CFBundleIdentifier": RUNNER_ID,
            "CFBundleName": "UltraTabUITests-Runner",
        }
    )
    (runner / "Info.plist").write_bytes(plistlib.dumps(info))
    test_bundle(
        sdk,
        developer,
        runner,
        "UltraTabUITests",
        sorted(str(path) for path in UI_SOURCES.glob("*.swift")),
    )
    test_bundle(
        sdk,
        developer,
        runner,
        "UltraTabTests",
        [str(APP_SOURCES / name) for name in LOGIC]
        + sorted(str(path) for path in UNIT_SOURCES.glob("*.swift")),
    )
    for item in sorted(frameworks.iterdir()):
        sign(item)
    sign(runner)
    return runner


def write_xctestrun(name, bundle, environment):
    path = PRODUCTS / f"{name}.xctestrun"
    path.write_bytes(
        plistlib.dumps(
            {
                bundle: {
                    "TestBundlePath": f"__TESTHOST__/PlugIns/{bundle}.xctest",
                    "TestHostPath": "__TESTROOT__/UltraTabUITests-Runner.app",
                    "TestHostBundleIdentifier": RUNNER_ID,
                    "UITargetAppPath": "__TESTROOT__/UltraTab.app",
                    "UITargetAppBundleIdentifier": APP_ID,
                    "IsUITestBundle": True,
                    "IsXCTRunnerHostedTestBundle": True,
                    "DependentProductPaths": [
                        "__TESTROOT__/UltraTab.app",
                        "__TESTROOT__/UltraTabUITests-Runner.app",
                    ],
                    "EnvironmentVariables": environment,
                    "TestingEnvironmentVariables": environment,
                    "SystemAttachmentLifetime": "deleteOnSuccess",
                    "UserAttachmentLifetime": "keepAlways",
                },
                "__xctestrun_metadata__": {"FormatVersion": 1},
            }
        )
    )
    return path


def run_xcodebuild(xctestrun, udid, results, only, log_name):
    command = [
        "xcodebuild",
        "test-without-building",
        "-xctestrun",
        str(xctestrun),
        "-destination",
        f"id={udid}",
        "-resultBundlePath",
        str(results),
        "-collect-test-diagnostics",
        "never",
    ]
    for method in only or []:
        command += ["-only-testing", method]
    log = BUILD / log_name
    with log.open("wb") as handle:
        outcome = subprocess.run(
            command, stdin=subprocess.DEVNULL, stdout=handle, stderr=handle
        )
    summary = [
        line
        for line in log.read_text(errors="replace").splitlines()
        if "Test Case" in line or "error:" in line or "** TEST" in line
    ]
    print("\n".join(summary[-60:]))
    return outcome.returncode


class MacClient(threading.Thread):
    """Holds an agent the way the lapis window does (attached at 100 columns)
    and records each line its screen shows and any status that ends it."""

    def __init__(self, agent):
        super().__init__(daemon=True)
        self.session = lapis_remote.WireSession(
            agent, 100, 30, mode=lapis_remote.DISCOVER
        )
        self.stopping = threading.Event()
        self.screen = ""
        self.columns = set()
        self.closed = None

    def run(self):
        while not self.stopping.is_set():
            try:
                received = self.session.receive(0.5)
            except (OSError, EOFError, lapis_remote.GatewayError) as error:
                if not self.stopping.is_set():
                    self.closed = str(error)
                return
            if received is None:
                continue
            kind, data = received
            if kind == lapis_remote.STATUS:
                self.closed = lapis_remote.status_message(data)
                return
            snapshot = self.session.accept_snapshot(kind, data)
            if snapshot is None:
                continue
            frame = lapis_remote.render_snapshot(snapshot)
            self.columns.add(frame["columns"])
            self.screen = "\n".join(
                "".join(run[0] for run in line) for line in frame["lines"]
            )

    def stop(self):
        self.stopping.set()
        self.join(5)
        self.session.close()


def fixture(run, runtime):
    """Registry, services, published state and composed cards. Returns the
    live agents' records and ids by name."""
    ids = {
        name: str(uuid.uuid4())
        for name in ("kernels", "docs", "approve", "parked", "trainer")
    }
    live = ("kernels", "docs")
    agents = []
    for name, identifier in ids.items():
        endpoint = (
            run.service(identifier, "/bin/sh", ["-c", ECHO], "/")
            if name in live
            else str(runtime / (identifier + ".sock"))
        )
        agents.append(
            {
                "id": identifier,
                "title": name,
                "category": "later" if name in ("parked", "trainer") else "build",
                "harness": "grok",
                "endpoint": endpoint,
                "program": "/bin/sh",
                "arguments": ["-c", ECHO],
                "directory": "/",
            }
        )
    (runtime / "workspace.json").write_text(
        json.dumps(
            {
                "version": 2,
                "activeCategory": "build",
                "categories": [
                    {"id": "build", "name": "Build"},
                    {"id": "later", "name": "Later"},
                ],
                "agents": agents,
            }
        )
    )
    state = {
        "version": 1,
        # This check stands in for the lapis window that publishes it.
        "pid": os.getpid(),
        "agents": [
            {
                "id": ids["kernels"],
                "status": "finished",
                "neededAtMs": 1000,
                "turnAtMs": 1000,
                "offer": {
                    "key": "guess-kernels",
                    "text": "Run the sweep",
                    "said": "The fused kernel is ready.",
                },
            },
            {
                "id": ids["docs"],
                "status": "finished",
                "unseen": True,
                "neededAtMs": 2000,
                "turnAtMs": 2000,
            },
            {
                "id": ids["approve"],
                "status": "waiting",
                "requests": 1,
                "request": "Allow rm -rf build/ in the approve folder? It waits on a "
                "permission prompt.",
                "neededAtMs": 3000,
            },
            {
                "id": ids["parked"],
                "status": "idle",
                "neededAtMs": 500,
                "offer": {
                    "key": "guess-parked",
                    "text": "Restart the parked job",
                    "seen": True,
                },
            },
            {"id": ids["trainer"], "status": "working"},
        ],
    }
    (runtime / "agent_state.json").write_text(json.dumps(state))
    cards = {
        "v": 1,
        "cards": {
            ids["kernels"]: {
                "key": "guess-kernels",
                "composed": "2026-10-07T06:01:45Z",
                "model": "fixture",
                "since": "You last looked 3 h ago; 2 turns since",
                "tldr": "The fused kernel is 2.1x faster; the sweep is ready to run",
                "blocks": [
                    {
                        "type": "text",
                        "text": "Fusing the softmax into the matmul epilogue removed one "
                        "global round trip; results match the reference on all 48 shapes.",
                    },
                    {
                        "type": "list",
                        "items": [
                            "Fused epilogue lands in kernels/fused.cu",
                            "Reference check passes on 48 of 48 shapes",
                            "The full sweep has not run yet",
                        ],
                    },
                    {
                        "type": "table",
                        "columns": ["shape", "before", "after", "speedup"],
                        "rows": [
                            ["4096²", "1.92 ms", "0.91 ms", "2.1x"],
                            ["8192²", "7.40 ms", "3.55 ms", "2.08x"],
                            ["16384²", "29.8 ms", "14.6 ms", "2.04x"],
                        ],
                    },
                ],
                "prompt": KERNELS_REPLY,
            },
            ids["docs"]: {
                "key": "turn:2000",
                "since": "You last looked 40 min ago; 1 turn since",
                "tldr": "The README diagram is redrawn with the gateway in the middle",
                "blocks": [
                    {"type": "diagram", "svg": DIAGRAM},
                    {
                        "type": "text",
                        "text": "Arrows still use the old blue; everything else follows "
                        "the new palette.",
                    },
                    {
                        "type": "link",
                        "label": "Architecture notes",
                        "url": "file:///tmp/ultratab-fixture/architecture.html",
                    },
                ],
                "prompt": "",
            },
        },
    }
    (runtime / "ultratab_cards.json").write_text(json.dumps(cards))
    workspace = lapis_remote.load_workspace(runtime / "workspace.json")
    records = {item["title"]: item for item in workspace["agents"]}
    return {name: records[name] for name in live}


def boot():
    state = simulator(["list", "devices", "available", "-j"]).stdout
    devices = [
        device
        for runtime_devices in json.loads(state)["devices"].values()
        for device in runtime_devices
        if device["name"] == DEVICE
    ]
    if not devices:
        raise SystemExit(f"no {DEVICE} simulator")
    device = devices[0]
    booted_here = device["state"] != "Booted"
    if booted_here:
        simulator(["boot", device["udid"]])
    simulator(["bootstatus", device["udid"], "-b"])
    simulator(
        [
            "status_bar",
            device["udid"],
            "override",
            "--time",
            "9:41",
            "--batteryLevel",
            "100",
        ],
        check=False,
    )
    return device["udid"], booted_here


def start_gateway(run, runtime):
    python = sys.executable
    tailscale = runtime / "tailscale-fixture"
    tailscale.write_text(
        f"#!{python}\n"
        "import json, sys\n"
        "if sys.argv[1:] != ['status', '--json']: sys.exit(1)\n"
        "print(json.dumps({'Self': {'UserID': 1, 'TailscaleIPs': [], "
        "'DNSName': 'simulator.invalid'}, "
        "'User': {'1': {'LoginName': 'simulator-fixture'}}}))\n"
    )
    tailscale.chmod(0o700)
    empty = runtime / "empty"
    empty.mkdir()
    gateway = run.start(
        "gateway",
        [
            python,
            str(ROOT / "apps" / "remote" / "lapis_remote.py"),
            "--registry",
            str(runtime / "workspace.json"),
            "--config",
            str(runtime / "lapis.json"),
            # Nothing of the person's: an empty home and agent histories.
            "--folders-home",
            str(empty),
            "--codex-home",
            str(empty),
            "--claude-home",
            str(empty),
            "--ssh-config",
            str(empty / "ssh_config"),
            "--bind",
            "127.0.0.1",
            "--port",
            str(PORT),
            "--allow-local",
            "--tailscale",
            str(tailscale),
        ],
    )
    deadline = time.monotonic() + 10
    while True:
        try:
            request = urllib.request.Request(
                f"http://127.0.0.1:{PORT}/api/deck", headers={"X-Lapis-Client": "check"}
            )
            with urllib.request.urlopen(request, timeout=1) as response:
                deck = json.load(response)
            if deck.get("state") and deck.get("cards"):
                return deck
        except (OSError, ValueError):
            pass
        if gateway.poll() is not None or time.monotonic() >= deadline:
            raise SystemExit(
                f"fixture gateway did not start; see {run.logs / 'gateway.log'}"
            )
        time.sleep(0.1)


def name_screens(screens):
    """Give each exported screenshot the name the test gave it."""
    manifest = screens / "manifest.json"
    if not manifest.exists():
        return
    for test in json.loads(manifest.read_text()):
        for attachment in test.get("attachments", []):
            exported = screens / attachment["exportedFileName"]
            name = attachment.get("suggestedHumanReadableName", "").split("_")[0]
            if exported.exists() and name:
                exported.rename(screens / f"{name}{exported.suffix}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--only", action="append", help="UI test method; repeat to select several"
    )
    parser.add_argument(
        "--unit", action="store_true", help="run only the UltraTabTests unit bundle"
    )
    args = parser.parse_args()
    if not args.unit and not SERVICE.exists():
        raise SystemExit(
            "missing the session service; build it first: "
            "cmake --build build/desktop --target lapis_session_service"
        )
    sdk = tool("xcrun", "--sdk", "iphonesimulator", "--show-sdk-path")
    platform = tool("xcrun", "--sdk", "iphonesimulator", "--show-sdk-platform-path")
    BUILD.mkdir(parents=True, exist_ok=True)
    build_app(sdk)
    build_runner(sdk, platform)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    results = BUILD / "results"
    results.mkdir(parents=True, exist_ok=True)
    udid, booted_here = boot()
    try:
        unit = run_xcodebuild(
            write_xctestrun("UltraTabUnit", "UltraTabTests", {}),
            udid,
            results / (stamp + "-unit.xcresult"),
            [],
            "test-unit.log",
        )
        print(f"unit results {results / (stamp + '-unit.xcresult')}")
        if args.unit:
            return unit
        return ui_check(args, udid, stamp, results) or unit
    finally:
        if booted_here:
            simulator(["shutdown", udid], check=False)


def ui_check(args, udid, stamp, results):
    runtime = Path(tempfile.mkdtemp(prefix="ultratab-ios-", dir="/tmp")).resolve()
    run = lapis_ios.Run(runtime)
    run.logs = BUILD / "remote-logs"
    run.logs.mkdir(parents=True, exist_ok=True)
    clients = {}
    try:
        live = fixture(run, runtime)
        start_gateway(run, runtime)
        for name, record in live.items():
            clients[name] = MacClient(record)
            clients[name].start()
        result = results / (stamp + ".xcresult")
        outcome = run_xcodebuild(
            write_xctestrun(
                "UltraTab", "UltraTabUITests", {"ULTRATAB_HOST": f"127.0.0.1:{PORT}"}
            ),
            udid,
            result,
            [f"UltraTabUITests/UltraTabUITests/{method}" for method in args.only or []],
            "test.log",
        )
        screens = BUILD / "screens" / stamp
        screens.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [
                "xcrun",
                "xcresulttool",
                "export",
                "attachments",
                "--path",
                str(result),
                "--output-path",
                str(screens),
            ],
            capture_output=True,
        )
        name_screens(screens)
        time.sleep(1)  # the last screens reach the Mac-side clients
        for client in clients.values():
            client.stop()
        expected = {"kernels": KERNELS_REPLY, "docs": DOCS_ANNOTATION}
        dealt = not args.only or "testDeal" in args.only
        ok = True
        for name, client in clients.items():
            got = [
                line[4:].rstrip()
                for line in client.screen.splitlines()
                if line.startswith("got:")
            ]
            print(
                f"Mac {name}: received {got}, closed {client.closed or 'never'}, "
                f"columns {sorted(client.columns)}"
            )
            if client.closed or client.columns != {100}:
                ok = False
            if dealt and got != [expected[name]]:
                ok = False
        print(f"results {result}\nscreens {screens}")
        return outcome or (0 if ok else 1)
    finally:
        for client in clients.values():
            if client.is_alive():
                client.stop()
        run.stop()
        shutil.rmtree(runtime, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
