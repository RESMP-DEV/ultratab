"""Build Ultra Tab for iPhone and install it with devicectl, through lapis's
installer (lapis/scripts/install_ios_app.py), which signs with an installed
Apple Development identity and a profile covering dev.lapis.ultratab.

    uv run --no-project python scripts/install_ios_app.py [--device ID]
        [--build-only] [--host HOST] [--wait MINUTES]
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "lapis" / "scripts"))
import install_ios_app as installer  # noqa: E402

installer.APPS = {
    "ultratab": ("dev.lapis.ultratab", ROOT / "ios" / "UltraTab", "UltraTab")
}
installer.PRODUCTS = ROOT / "build" / "ios" / "device"

if __name__ == "__main__":
    sys.argv[1:1] = ["--app", "ultratab"]
    sys.exit(installer.main())
