"""Verify that ESP-IDF network throughput overrides reached sdkconfig.h."""

from pathlib import Path

Import("env")  # noqa: F821 -- provided by PlatformIO


REQUIRED_KEYS = (
    "CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM",
    "CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM",
    "CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM",
    "CONFIG_ESP_WIFI_AMPDU_TX_ENABLED",
    "CONFIG_ESP_WIFI_TX_BA_WIN",
    "CONFIG_ESP_WIFI_AMPDU_RX_ENABLED",
    "CONFIG_ESP_WIFI_RX_BA_WIN",
    "CONFIG_LWIP_TCP_SND_BUF_DEFAULT",
    "CONFIG_LWIP_TCP_WND_DEFAULT",
    "CONFIG_LWIP_TCP_RECVMBOX_SIZE",
    "CONFIG_LWIP_UDP_RECVMBOX_SIZE",
    "CONFIG_LWIP_TCPIP_RECVMBOX_SIZE",
    "CONFIG_LWIP_TCPIP_TASK_STACK_SIZE",
    "CONFIG_LWIP_TCP_FAST_RETRANSMISSION",
    "CONFIG_ESP_WIFI_RX_IRAM_OPT",
    "CONFIG_ESP_WIFI_SLP_IRAM_OPT",
    "CONFIG_FREERTOS_HZ",
    "CONFIG_AUTOSTART_ARDUINO",
    "CONFIG_ESPTOOLPY_FLASHSIZE_16MB",
)


def parse_defaults(path: Path) -> dict[str, str]:
    values = {}
    with path.open("r", encoding="utf-8") as config:
        for raw_line in config:
            line = raw_line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            if key in REQUIRED_KEYS:
                values[key] = "1" if value == "y" else value
    return values


def parse_header(path: Path) -> dict[str, str]:
    values = {}
    with path.open("r", encoding="utf-8") as config:
        for raw_line in config:
            parts = raw_line.strip().split(maxsplit=2)
            if len(parts) >= 3 and parts[0] == "#define" and parts[1] in REQUIRED_KEYS:
                values[parts[1]] = parts[2]
    return values


def verify_network_sdkconfig(target, source, env):
    del target, source
    project_dir = Path(env.subst("$PROJECT_DIR")).resolve()
    build_dir = Path(env.subst("$BUILD_DIR")).resolve()
    expected = parse_defaults(project_dir / "sdkconfig.defaults")
    generated_path = build_dir / "config" / "sdkconfig.h"
    if not generated_path.exists():
        print(f"ERROR: generated sdkconfig is missing: {generated_path}")
        env.Exit(1)
    generated = parse_header(generated_path)
    missing = [key for key in REQUIRED_KEYS if key not in expected]
    mismatches = [
        (key, expected.get(key), generated.get(key))
        for key in REQUIRED_KEYS
        if expected.get(key) != generated.get(key)
    ]
    if missing or mismatches:
        print("ERROR: network sdkconfig overrides were not applied")
        for key in missing:
            print(f"  missing default: {key}")
        for key, expected_value, actual_value in mismatches:
            print(f"  {key}: expected {expected_value}, got {actual_value}")
        env.Exit(1)
    print("Verified X3 network sdkconfig overrides:")
    for key in REQUIRED_KEYS:
        print(f"  {key}={generated[key]}")


env.AddPostAction("$BUILD_DIR/${PROGNAME}${PROGSUFFIX}", verify_network_sdkconfig)  # noqa: F821
