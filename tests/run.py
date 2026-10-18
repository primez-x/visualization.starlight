"""Build and run the host spectrum checks outside the source tree."""
import pathlib
import subprocess
import tempfile
import xml.etree.ElementTree as ET

root = pathlib.Path(__file__).resolve().parents[1]
manifest = ET.parse(root / "addon.xml").getroot()
assert manifest.find("extension[@point='xbmc.player.musicviz']").get("library_linux") == "visualization.starlight.so"
with tempfile.TemporaryDirectory(prefix="starlight-host-") as build:
    subprocess.run(["cmake", "-S", str(root), "-B", build,
                    "-DSTARLIGHT_BUILD_ADDON=OFF", "-DCMAKE_BUILD_TYPE=Release"], check=True)
    subprocess.run(["cmake", "--build", build, "-j2"], check=True)
    subprocess.run(["ctest", "--test-dir", build, "--output-on-failure"], check=True)
