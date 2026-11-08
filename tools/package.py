"""Build a Data-root mod archive from verified upstream assets and the new DLL."""
import argparse
import hashlib
from pathlib import Path, PurePosixPath
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM_SHA256 = "02b77372a6828a90750a47f515c6a9377339fad44f471c02f41a60a438f22a5f"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--assets", type=Path, required=True)
parser.add_argument("--dll", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
if hashlib.sha256(args.assets.read_bytes()).hexdigest() != UPSTREAM_SHA256:
    raise ValueError("Expected the verified upstream v0.2.0-NG asset archive")
if args.dll.suffix.lower() != ".dll" or not args.dll.is_file():
    raise ValueError("Missing compiled plugin DLL")
args.output.parent.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="valhalla-package-", dir=ROOT / "build") as temporary:
    temporary = Path(temporary)
    source = temporary / "original" / "ValhallaCombat.esp"
    source.parent.mkdir()
    migrated = temporary / "migrated" / "ValhallaCombat.esp"
    with zipfile.ZipFile(args.assets) as upstream:
        source.write_bytes(upstream.read("ValhallaCombat.esp"))
        subprocess.run(["dotnet", "restore", str(ROOT / "tools/AssetMigration/AssetMigration.csproj"), "--source", "https://api.nuget.org/v3/index.json"], check=True)
        subprocess.run(["dotnet", "run", "--project", str(ROOT / "tools/AssetMigration"), "--no-restore", "--", str(source), str(migrated)], check=True)
        with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED) as output:
            for entry in upstream.infolist():
                name = PurePosixPath(entry.filename)
                if name.is_absolute() or ".." in name.parts:
                    raise ValueError("Unsafe upstream archive path")
                lower = entry.filename.lower()
                if entry.is_dir() or lower.startswith(("mcm/", "interface/", "scripts/")) or lower.endswith(".dll") or lower == "valhallacombat.esp":
                    continue
                output.writestr(entry.filename, upstream.read(entry))
            output.write(migrated, "ValhallaCombat.esp")
            output.write(args.dll, "SKSE/Plugins/valhallaCombat.dll")
            output.write(ROOT / "docs/INSTALL-MenuFramework.md", "ValhallaCombat-Install.md")
            output.write(ROOT / "docs/GAMEPLAY.md", "ValhallaCombat-Gameplay.md")
            for rule in sorted((ROOT / "config/EnemyClassification").glob("*.ini")):
                output.write(rule, "SKSE/Plugins/ValhallaCombat/EnemyClassification/" + rule.name)
            output.write(ROOT / "src/include/vendor/MenuFramework/LICENSE", "licenses/SKSE-Menu-Framework-LICENSE.txt")
    with zipfile.ZipFile(args.output) as archive:
        names = archive.namelist()
        assert not any(name.lower().startswith(("mcm/", "scripts/", "interface/")) for name in names)
        assert "SKSE/Plugins/ValhallaCombat/Killmoves.ini" in names
        assert "SKSE/Plugins/ValhallaCombat/RaceMapping/Vanilla.ini" in names
        assert "SKSE/Plugins/valhallaCombat.dll" in names
        assert "SKSE/Plugins/ValhallaCombat/EnemyClassification/00-Example.ini" in names
        assert len(names) == len(set(name.lower() for name in names))
digest = hashlib.sha256(args.output.read_bytes()).hexdigest()
args.output.with_suffix(".zip.sha256").write_text(digest + "  " + args.output.name + "\n", encoding="utf-8")
print(f"Verified package: {args.output}\nSHA256: {digest}")
