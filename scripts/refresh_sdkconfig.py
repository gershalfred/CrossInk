"""Discard a stale generated sdkconfig when tracked defaults changed."""

from pathlib import Path

Import("env")  # noqa: F821 -- provided by PlatformIO


def parse_assignments(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.exists():
        return values
    with path.open("r", encoding="utf-8") as config:
        for raw_line in config:
            line = raw_line.strip()
            if line.startswith("# CONFIG_") and line.endswith(" is not set"):
                values[line[2 : -len(" is not set")]] = "n"
                continue
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            values[key] = value
    return values


project_dir = Path(env.subst("$PROJECT_DIR")).resolve()
environment = env.subst("$PIOENV")
defaults_path = project_dir / "sdkconfig.defaults"
generated_path = project_dir / f"sdkconfig.{environment}"

expected = parse_assignments(defaults_path)
generated = parse_assignments(generated_path)
mismatches = [key for key, value in expected.items() if generated.get(key) != value]

if generated_path.exists() and mismatches:
    generated_path.unlink()
    print(
        f"Removed stale {generated_path.name}; "
        f"{len(mismatches)} tracked sdkconfig default(s) changed"
    )
