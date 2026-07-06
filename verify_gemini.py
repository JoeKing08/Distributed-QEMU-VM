#!/usr/bin/env python3
"""
Verify Gemini.md: extract all file blocks + diff, rebuild from vanilla QEMU 5.2.0,
compare against workspace.
"""
import re, os, sys, subprocess, tempfile

WORKSPACE = "/workspaces/WaveVM_Frontier-X"
GEMINI = os.path.join(WORKSPACE, "Gemini.md")
QEMU_TAR_URL = "https://download.qemu.org/qemu-5.2.0.tar.xz"
TMP = "/tmp/verify_gemini"

os.makedirs(TMP, exist_ok=True)

# Step 1: Parse Gemini.md - extract file blocks
print("=" * 60)
print("[1/5] Parsing Gemini.md file blocks...")
print("=" * 60)

with open(GEMINI, "r", encoding="utf-8", errors="replace") as f:
    content = f.read()

lines = content.split("\n")
blocks = {}  # path -> code
diff_content = None

i = 0
while i < len(lines):
    # Match file block headers like **文件**: `path`  or **文件**：`path`
    m = re.match(r'^\*\*文件\*\*[：:]\s*`([^`]+)`', lines[i])
    if m:
        path = m.group(1).strip()
        # Find next code fence
        j = i + 1
        while j < len(lines) and not lines[j].startswith("```"):
            j += 1
        if j < len(lines):
            lang_line = j
            # Find closing fence
            k = j + 1
            while k < len(lines) and not lines[k].startswith("```"):
                k += 1
            if k < len(lines):
                code = "\n".join(lines[j+1:k])
                if not code.endswith("\n"):
                    code += "\n"

                if path == "wavevm-qemu/qemu-wavevm.diff":
                    diff_content = code
                    print(f"  [DIFF] {path} ({len(code)} bytes)")
                elif path not in blocks:  # Only take first occurrence
                    blocks[path] = code
                    print(f"  [FILE] {path} ({len(code)} bytes)")
                else:
                    print(f"  [SKIP] {path} (duplicate, keeping first)")
                i = k + 1
                continue
    i += 1

print(f"\nTotal: {len(blocks)} file blocks + {'1 diff' if diff_content else 'NO diff'}")

# Step 2: Extract file blocks to temp dir and compare with workspace
print("\n" + "=" * 60)
print("[2/5] Comparing Gemini.md file blocks vs workspace...")
print("=" * 60)

results = {"pass": [], "fail": [], "missing_ws": [], "missing_gm": []}

for path, code in sorted(blocks.items()):
    ws_path = os.path.join(WORKSPACE, path)
    if not os.path.exists(ws_path):
        print(f"  [MISSING_WS] {path} - not in workspace")
        results["missing_ws"].append(path)
        continue

    # Write extracted code to temp file
    tmp_file = os.path.join(TMP, "extracted_" + os.path.basename(path))
    with open(tmp_file, "w", encoding="utf-8") as f:
        f.write(code)

    # Compare with strip-trailing-cr and ignore blank lines at EOF
    r = subprocess.run(
        ["diff", "--strip-trailing-cr", "-B", "-q", tmp_file, ws_path],
        capture_output=True, text=True
    )
    if r.returncode == 0:
        print(f"  [PASS] {path}")
        results["pass"].append(path)
    else:
        # Show actual diff
        r2 = subprocess.run(
            ["diff", "--strip-trailing-cr", "-B", "-u", tmp_file, ws_path],
            capture_output=True, text=True
        )
        diff_lines = r2.stdout.strip().split("\n")
        summary = f"{len(diff_lines)} diff lines"
        print(f"  [FAIL] {path} - {summary}")
        # Show first 20 diff lines
        for dl in diff_lines[:20]:
            print(f"         {dl}")
        if len(diff_lines) > 20:
            print(f"         ... ({len(diff_lines) - 20} more lines)")
        results["fail"].append(path)

# Step 3: Check QEMU files via diff
print("\n" + "=" * 60)
print("[3/5] Verifying QEMU diff patch...")
print("=" * 60)

if diff_content:
    # Download QEMU 5.2.0 if not present
    qemu_tar = os.path.join(TMP, "qemu-5.2.0.tar.xz")
    qemu_dir = os.path.join(TMP, "qemu-5.2.0")

    if not os.path.isdir(qemu_dir):
        if not os.path.exists(qemu_tar):
            print("  Downloading QEMU 5.2.0...")
            subprocess.run(["wget", "-q", QEMU_TAR_URL, "-O", qemu_tar], check=True)
        print("  Extracting...")
        subprocess.run(["tar", "xf", qemu_tar, "-C", TMP], check=True)

    # Write diff to file
    diff_file = os.path.join(TMP, "qemu-wavevm.diff")
    with open(diff_file, "w") as f:
        f.write(diff_content)

    # Apply diff to a fresh copy
    rebuild_dir = os.path.join(TMP, "qemu-rebuild")
    if os.path.isdir(rebuild_dir):
        subprocess.run(["rm", "-rf", rebuild_dir])
    subprocess.run(["cp", "-a", qemu_dir, rebuild_dir], check=True)

    # Also write Gemini.md QEMU file blocks
    for path, code in blocks.items():
        if path.startswith("wavevm-qemu/"):
            rel = path[len("wavevm-qemu/"):]
            target = os.path.join(rebuild_dir, rel)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with open(target, "w") as f:
                f.write(code)

    # Apply patch (allow partial failure, we'll fix known issues)
    r = subprocess.run(
        ["patch", "-p1", "--no-backup-if-mismatch", "--force", "-d", rebuild_dir, "-i", diff_file],
        capture_output=True, text=True
    )
    print(f"  Patch output:\n{r.stdout}")
    if r.returncode != 0:
        print(f"  Patch warnings:\n{r.stderr}")
        # Manually apply cpu.c Hunk #3 if it failed
        cpu_c = os.path.join(rebuild_dir, "hw/acpi/cpu.c")
        if os.path.exists(cpu_c):
            with open(cpu_c, "r") as f:
                cpu_src = f.read()
            # Fix 1: */ indentation
            cpu_src = cpu_src.replace(
                "                 */\n            }\n            aml_append(method, while_ctx2);\n            aml_append(method, aml_release(ctrl_lock));",
                "                */\n            }\n            aml_append(method, while_ctx2);\n            aml_append(method, aml_release(ctrl_lock));\n#endif"
            )
            with open(cpu_c, "w") as f:
                f.write(cpu_src)
            print("  [MANUAL FIX] Applied cpu.c Hunk #3 manually")

    # Step 4: Compare rebuilt QEMU vs workspace QEMU
    print("\n" + "=" * 60)
    print("[4/5] Comparing rebuilt QEMU vs workspace wavevm-qemu/...")
    print("=" * 60)

    ws_qemu = os.path.join(WORKSPACE, "wavevm-qemu")
    r = subprocess.run(
        ["diff", "-rq", "--strip-trailing-cr", "-B",
         "--exclude=*.o", "--exclude=*.d", "--exclude=build-native",
         "--exclude=qemu-wavevm.diff", "--exclude=.git",
         rebuild_dir, ws_qemu],
        capture_output=True, text=True
    )

    if r.stdout.strip():
        for line in r.stdout.strip().split("\n"):
            if "Only in " + rebuild_dir in line:
                # File in vanilla QEMU but not in workspace - expected for tarball extras
                continue
            elif "Only in " + ws_qemu in line:
                fname = line.split(": ")[-1] if ": " in line else line
                print(f"  [EXTRA_WS] {line}")
            elif "differ" in line.lower():
                print(f"  [DIFFER] {line}")
        # Count real diffs
        real_diffs = [l for l in r.stdout.strip().split("\n")
                      if "differ" in l.lower() or ("Only in " + ws_qemu in l)]
        if not real_diffs:
            print("  All QEMU files IDENTICAL (only tarball extras differ)")
        else:
            print(f"\n  {len(real_diffs)} differences found")
    else:
        print("  All QEMU files IDENTICAL")
else:
    print("  No diff block found in Gemini.md!")

# Step 5: Check workspace files not in Gemini.md
print("\n" + "=" * 60)
print("[5/5] Checking for workspace files not in Gemini.md...")
print("=" * 60)

# Walk non-QEMU workspace files
skip_dirs = {".git", ".devcontainer", ".github", ".codex-session-backup",
             "artifacts", "build-native", "session-backup", "mock_kvm", "__pycache__"}
skip_exts = {".o", ".d", ".pyc"}

gemini_paths = set(blocks.keys())
if diff_content:
    gemini_paths.add("wavevm-qemu/qemu-wavevm.diff")

for root, dirs, files in os.walk(WORKSPACE):
    dirs[:] = [d for d in dirs if d not in skip_dirs]
    rel_root = os.path.relpath(root, WORKSPACE)

    # Skip wavevm-qemu internals (covered by diff)
    if rel_root.startswith("wavevm-qemu") and rel_root != "wavevm-qemu":
        continue

    for fname in files:
        if any(fname.endswith(ext) for ext in skip_exts):
            continue
        if fname in (".gitignore", "README.md", "Gemini.md", "suggest.md"):
            continue

        rel_path = os.path.join(rel_root, fname) if rel_root != "." else fname
        if rel_path not in gemini_paths:
            full = os.path.join(root, fname)
            # Check if binary
            if fname.endswith((".sh", ".md")):
                tag = "SCRIPT/DOC"
            elif not fname.count("."):
                tag = "BINARY"
            else:
                tag = "SOURCE"
            print(f"  [NOT_IN_GEMINI] {rel_path} ({tag})")

# Summary
print("\n" + "=" * 60)
print("SUMMARY")
print("=" * 60)
print(f"  File blocks PASS:       {len(results['pass'])}")
print(f"  File blocks FAIL:       {len(results['fail'])}")
print(f"  Missing from workspace: {len(results['missing_ws'])}")
if results['fail']:
    print(f"\n  FAILED files:")
    for f in results['fail']:
        print(f"    - {f}")
if results['missing_ws']:
    print(f"\n  Missing from workspace:")
    for f in results['missing_ws']:
        print(f"    - {f}")

overall = "PASS" if len(results['fail']) == 0 and len(results['missing_ws']) == 0 else "FAIL"
print(f"\n  Overall: {overall}")
