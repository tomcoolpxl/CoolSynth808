"""Package CoolSynth 808 for macOS (CoolSynth's D370, as CoolSynth's own installer/macos/package.py, its D234 and D272).

usage: python3 installer/macos/package.py <artefacts folder> <output folder> <name>
   e.g. python3 installer/macos/package.py build/ci/CoolSynth808_artefacts/Release build/ci/packages v0.2.0

The bundles are copied, the licence and notices put in the app, and each bundle signed ad hoc as a whole (the linker
signs only the executable; Apple silicon runs nothing unsigned). Then, in the output folder:
- CoolSynth808-macos-arm64-<name>.zip: the app, the VST3, the licence and the notices, as the Windows zip.
- CoolSynth808-macos-arm64-clap-<name>.zip: the CLAP.
- CoolSynth808-Setup-<name>.pkg: installs for the current user only, with no administrator password, into their home
  folder: the app in ~/Applications, the VST3 and the CLAP in ~/Library/Audio/Plug-Ins. A choice per format. Unsigned,
  so the player clicks Open Anyway once; what it installs carries no quarantine flag.
- CoolSynth808-macos-arm64-sha256-<name>.txt: the checksums of the other files.
"""
import hashlib
import os
import plistlib
import re
import shutil
import subprocess
import sys
from xml.sax.saxutils import escape, quoteattr

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
ID = "com.tomcoolpxl.coolsynth808"
PRODUCT = "CoolSynth 808"


def run(*args):
    print("+", " ".join(args), flush=True)
    subprocess.run(args, check=True)


def project_version():
    with open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8") as h:
        return re.search(r"project\(CoolSynth808 VERSION (\d+\.\d+\.\d+)", h.read()).group(1)


def sign(bundle):
    run("codesign", "--force", "--sign", "-", "--timestamp=none", bundle)
    run("codesign", "--verify", "--strict", bundle)


def payload_package(stage, identifier, bundle, location, version, out):
    """A component package laying down one bundle at location (a path inside the home folder)."""
    root = os.path.join(stage, "roots", identifier)
    os.makedirs(root)
    shutil.copytree(bundle, os.path.join(root, os.path.basename(bundle)), symlinks=True)
    plist = os.path.join(stage, identifier + ".plist")
    run("pkgbuild", "--analyze", "--root", root, plist)
    with open(plist, "rb") as h:
        components = plistlib.load(h)
    for c in components:
        c["BundleIsRelocatable"] = False  # an update replaces this copy, not one the player moved elsewhere
        c["BundleOverwriteAction"] = "upgrade"
    with open(plist, "wb") as h:
        plistlib.dump(components, h)
    run("pkgbuild", "--root", root, "--component-plist", plist, "--identifier", identifier, "--version", version,
        "--install-location", location, out)


def main():
    artefacts, output, name = sys.argv[1], sys.argv[2], sys.argv[3]
    version = project_version()
    stage = os.path.join(output, ".staging")
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(os.path.join(stage, "pkgs"))

    # The bundles, signed whole; the licence and notices inside the app.
    app = os.path.join(stage, f"{PRODUCT}.app")
    vst3 = os.path.join(stage, f"{PRODUCT}.vst3")
    clap = os.path.join(stage, f"{PRODUCT}.clap")
    shutil.copytree(os.path.join(artefacts, "Standalone", f"{PRODUCT}.app"), app, symlinks=True)
    shutil.copytree(os.path.join(artefacts, "VST3", f"{PRODUCT}.vst3"), vst3, symlinks=True)
    shutil.copytree(os.path.join(artefacts, "CLAP", f"{PRODUCT}.clap"), clap, symlinks=True)
    resources = os.path.join(app, "Contents", "Resources")
    shutil.copy(os.path.join(ROOT, "LICENSE"), os.path.join(resources, "LICENSE.txt"))
    shutil.copy(os.path.join(ROOT, "THIRD_PARTY_NOTICES.txt"), resources)
    for bundle in (app, vst3, clap):
        sign(bundle)

    # The zips, as Windows': the app, the VST3, the licence and the notices in one; the CLAP in its own.
    files = []
    zipped = os.path.join(stage, "zip")
    os.makedirs(zipped)
    for bundle in (app, vst3):
        shutil.copytree(bundle, os.path.join(zipped, os.path.basename(bundle)), symlinks=True)
    shutil.copy(os.path.join(ROOT, "LICENSE"), os.path.join(zipped, "LICENSE.txt"))
    shutil.copy(os.path.join(ROOT, "THIRD_PARTY_NOTICES.txt"), zipped)
    main_zip = os.path.join(output, f"CoolSynth808-macos-arm64-{name}.zip")
    # ditto keeps the bundles' symlinks and signatures, as Finder's own zips do.
    run("ditto", "-c", "-k", "--sequesterRsrc", zipped, main_zip)
    files.append(main_zip)
    clap_zip = os.path.join(output, f"CoolSynth808-macos-arm64-clap-{name}.zip")
    run("ditto", "-c", "-k", "--keepParent", clap, clap_zip)
    files.append(clap_zip)

    # The installer: one package per format.
    pkgs = os.path.join(stage, "pkgs")
    payload_package(stage, f"{ID}.app", app, "/Applications", version, os.path.join(pkgs, "app.pkg"))
    payload_package(stage, f"{ID}.vst3", vst3, "/Library/Audio/Plug-Ins/VST3", version, os.path.join(pkgs, "vst3.pkg"))
    payload_package(stage, f"{ID}.clap", clap, "/Library/Audio/Plug-Ins/CLAP", version, os.path.join(pkgs, "clap.pkg"))

    def choice(cid, title, description, pkg):
        return (f'  <choice id="{cid}" title={quoteattr(title)} description={quoteattr(description)}>'
                f'<pkg-ref id="{ID}.{pkg}"/></choice>\n')

    distribution = (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<installer-gui-script minSpecVersion="2">\n'
        f'  <title>{escape(PRODUCT)} {escape(version)}</title>\n'
        f'  <product id="{ID}" version="{version}"/>\n'
        '  <welcome file="welcome.html" mime-type="text/html"/>\n'
        '  <options customize="allow" require-scripts="false" hostArchitectures="arm64"/>\n'
        '  <domains enable_anywhere="false" enable_currentUserHome="true" enable_localSystem="false"/>\n'
        '  <volume-check><allowed-os-versions><os-version min="13.0"/></allowed-os-versions></volume-check>\n'
        '  <choices-outline>\n    <line choice="app"/>\n    <line choice="vst3"/>\n    <line choice="clap"/>\n'
        '  </choices-outline>\n'
        + choice("app", "Standalone app", f"{PRODUCT} on its own, in the Applications folder of your home folder.", "app")
        + choice("vst3", "VST3 plugin", f"To play {PRODUCT} in your music software: Ableton Live, FL Studio, Reaper, "
                 "Bitwig, Cubase, Studio One.", "vst3")
        + choice("clap", "CLAP plugin", "For music software that supports CLAP: Bitwig, Reaper, FL Studio, Studio One.", "clap")
        + "".join(f'  <pkg-ref id="{ID}.{r}" version="{version}">{r}.pkg</pkg-ref>\n' for r in ("app", "vst3", "clap"))
        + '</installer-gui-script>\n')
    distribution_path = os.path.join(stage, "distribution.xml")
    with open(distribution_path, "w", encoding="utf-8") as h:
        h.write(distribution)
    resources_dir = os.path.join(stage, "resources")
    os.makedirs(resources_dir)
    shutil.copy(os.path.join(HERE, "welcome.html"), resources_dir)

    installer = os.path.join(output, f"CoolSynth808-Setup-{name}.pkg")
    run("productbuild", "--distribution", distribution_path, "--package-path", pkgs, "--resources", resources_dir,
        installer)
    files.append(installer)

    with open(os.path.join(output, f"CoolSynth808-macos-arm64-sha256-{name}.txt"), "w", encoding="utf-8") as h:
        for path in files:
            digest = hashlib.sha256()
            with open(path, "rb") as f:
                for block in iter(lambda: f.read(1 << 20), b""):
                    digest.update(block)
            h.write(f"{digest.hexdigest()}  {os.path.basename(path)}\n")
    shutil.rmtree(stage)
    print("packaged into", output)


if __name__ == "__main__":
    main()
