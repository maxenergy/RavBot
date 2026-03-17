#!/bin/bash
# RavBot Onboard Integration Test
#
# Validates the full onboard flow and key CLI commands without requiring a
# running gateway or a real API key.
#
# Usage:
#   bash tests/test-onboard.sh [/path/to/ravbot]
#
# Exit code: 0 = all tests passed, non-zero = failures detected.
#
# CI: run after `cmake --build build --parallel` succeeds.

set -uo pipefail

# ---------- Configuration ----------

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BINARY="${1:-${REPO_ROOT}/build/ravbot}"
TEST_HOME="/tmp/ravbot-onboard-$$"
PASS=0
FAIL=0

# ---------- Helpers ----------

cleanup() { rm -rf "$TEST_HOME"; }
trap cleanup EXIT

pass() { echo "  [PASS] $1"; PASS=$((PASS + 1)); }
fail() { echo "  [FAIL] $1: $2"; FAIL=$((FAIL + 1)); }

qc() { HOME="$TEST_HOME" "$BINARY" "$@"; }

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "ERROR: required command '$1' not found in PATH"
        exit 1
    fi
}

# ---------- Pre-flight ----------

echo "=== RavBot Onboard Integration Test ==="
echo "Binary : $BINARY"
echo "TestDir: $TEST_HOME"
echo ""

if [[ ! -x "$BINARY" ]]; then
    echo "ERROR: binary not found or not executable: $BINARY"
    echo "  Build first: cmake --build build --parallel"
    exit 1
fi

require_cmd python3
mkdir -p "$TEST_HOME"

# ---------- Phase 1: Initial onboard --quick ----------

echo "--- Phase 1: Initial onboard ---"

qc onboard --quick >"$TEST_HOME/onboard.log" 2>&1
RC=$?

if [[ $RC -eq 0 ]]; then
    pass "O1.1 onboard --quick exits 0"
else
    fail "O1.1 onboard --quick exits 0" "exit code was $RC (log: $TEST_HOME/onboard.log)"
fi

# ---------- Phase 2: Workspace directory structure ----------

echo ""
echo "--- Phase 2: Workspace structure ---"

WS="$TEST_HOME/.ravbot/agents/main/workspace"

if [[ -d "$WS" ]]; then
    pass "O2.1 workspace at agents/main/workspace"
else
    fail "O2.1 workspace at agents/main/workspace" "not found"
fi

# Must NOT use legacy agents/default/ path
if [[ ! -d "$TEST_HOME/.ravbot/agents/default" ]]; then
    pass "O2.2 no legacy agents/default/ directory"
else
    fail "O2.2 no legacy agents/default/ directory" "agents/default/ exists"
fi

# sessions dir
if [[ -d "$TEST_HOME/.ravbot/agents/main/sessions" ]]; then
    pass "O2.3 sessions directory created"
else
    fail "O2.3 sessions directory created" "not found"
fi

# ---------- Phase 3: Workspace files (all 8 required) ----------

echo ""
echo "--- Phase 3: Workspace files ---"

REQUIRED_FILES=(SOUL.md MEMORY.md SKILL.md IDENTITY.md HEARTBEAT.md USER.md AGENTS.md TOOLS.md)
for f in "${REQUIRED_FILES[@]}"; do
    if [[ -s "$WS/$f" ]]; then
        pass "O3.$(( PASS + FAIL )) $f created and non-empty"
    else
        fail "O3.$(( PASS + FAIL )) $f" "missing or empty at $WS/$f"
    fi
done

# ---------- Phase 4: Configuration file ----------

echo ""
echo "--- Phase 4: Config file ---"

CFG="$TEST_HOME/.ravbot/ravbot.json"

if [[ -f "$CFG" ]]; then
    pass "O4.1 ravbot.json exists"
else
    fail "O4.1 ravbot.json exists" "not found"
fi

if python3 -c "import json; json.load(open('$CFG'))" 2>/dev/null; then
    pass "O4.2 ravbot.json is valid JSON"
else
    fail "O4.2 ravbot.json is valid JSON" "parse error"
fi

for key in agent gateway models; do
    if python3 -c "import json,sys; d=json.load(open('$CFG')); sys.exit(0 if '$key' in d else 1)" 2>/dev/null; then
        pass "O4.3 config has '$key' section"
    else
        fail "O4.3 config has '$key' section" "key missing"
    fi
done

# Gateway auth token must be auto-generated (non-empty)
TOKEN=$(python3 -c "import json; d=json.load(open('$CFG')); print(d.get('gateway',{}).get('auth',{}).get('token',''))" 2>/dev/null)
if [[ -n "$TOKEN" ]]; then
    pass "O4.4 gateway auth token auto-generated"
else
    fail "O4.4 gateway auth token auto-generated" "token is empty"
fi

# ---------- Phase 5: Built-in skills ----------

echo ""
echo "--- Phase 5: Built-in skills ---"

SKILLS_DIR="$TEST_HOME/.ravbot/skills"
EXPECTED_SKILLS=(search weather github healthcheck skill-creator)
for skill in "${EXPECTED_SKILLS[@]}"; do
    if [[ -d "$SKILLS_DIR/$skill" ]]; then
        pass "O5.$(( PASS + FAIL )) skill '$skill' installed"
    else
        fail "O5.$(( PASS + FAIL )) skill '$skill'" "directory missing in $SKILLS_DIR"
    fi
done

# ---------- Phase 6: Config CLI commands ----------

echo ""
echo "--- Phase 6: Config CLI commands ---"

# config validate
OUT=$(qc config validate 2>&1)
if echo "$OUT" | grep -qi "valid"; then
    pass "O6.1 config validate reports valid"
else
    fail "O6.1 config validate reports valid" "output: $OUT"
fi

# config schema — must mention known fields
OUT=$(qc config schema 2>&1)
if echo "$OUT" | grep -qi "agent\|gateway\|model"; then
    pass "O6.2 config schema shows known fields"
else
    fail "O6.2 config schema shows known fields" "output: $OUT"
fi

# config get (full dump)
OUT=$(qc config get 2>&1)
if echo "$OUT" | grep -qi "agent\|gateway"; then
    pass "O6.3 config get returns config"
else
    fail "O6.3 config get returns config" "output: $OUT"
fi

# ---------- Phase 7: Re-onboard idempotency & API key preservation (B6) ----------

echo ""
echo "--- Phase 7: Re-onboard idempotency ---"

# Inject a sentinel API key
python3 - <<PYEOF 2>/dev/null
import json
cfg_path = "$CFG"
with open(cfg_path) as f:
    cfg = json.load(f)
cfg.setdefault('models', {}).setdefault('providers', {}).setdefault('anthropic', {})['apiKey'] = 'sk-ant-sentinel-key-abc123'
with open(cfg_path, 'w') as f:
    json.dump(cfg, f, indent=2)
PYEOF

# Re-run onboard --quick
qc onboard --quick >"$TEST_HOME/reonboard.log" 2>&1
RC=$?

if [[ $RC -eq 0 ]]; then
    pass "O7.1 re-onboard --quick exits 0"
else
    fail "O7.1 re-onboard --quick exits 0" "exit code $RC"
fi

# API key must be preserved (deep-merge, not overwritten)
KEY=$(python3 -c "import json; d=json.load(open('$CFG')); print(d.get('models',{}).get('providers',{}).get('anthropic',{}).get('apiKey',''))" 2>/dev/null)
if [[ "$KEY" == "sk-ant-sentinel-key-abc123" ]]; then
    pass "O7.2 re-onboard preserves existing API key"
else
    fail "O7.2 re-onboard preserves existing API key" "key after re-onboard: '$KEY'"
fi

# Workspace files survive re-onboard
for f in SOUL.md MEMORY.md; do
    if [[ -s "$WS/$f" ]]; then
        pass "O7.3 $f survives re-onboard"
    else
        fail "O7.3 $f" "missing or empty after re-onboard"
    fi
done

# ---------- Phase 8: onboard --help (smoke) ----------

echo ""
echo "--- Phase 8: Help / version smoke ---"

if HOME="$TEST_HOME" "$BINARY" onboard --help >/dev/null 2>&1; then
    pass "O8.1 onboard --help exits 0"
else
    # Some tools exit non-zero from --help; accept either
    OUT=$(HOME="$TEST_HOME" "$BINARY" onboard --help 2>&1 || true)
    if echo "$OUT" | grep -qi "onboard\|quick\|wizard"; then
        pass "O8.1 onboard --help shows usage"
    else
        fail "O8.1 onboard --help shows usage" "output: $OUT"
    fi
fi

if HOME="$TEST_HOME" "$BINARY" --version >/dev/null 2>&1; then
    pass "O8.2 --version exits 0"
else
    OUT=$(HOME="$TEST_HOME" "$BINARY" --version 2>&1 || true)
    if echo "$OUT" | grep -qi "ravbot\|version\|[0-9]\+\.[0-9]\+"; then
        pass "O8.2 --version shows version info"
    else
        fail "O8.2 --version shows version info" "output: $OUT"
    fi
fi

# ---------- Summary ----------

echo ""
echo "========================================"
TOTAL=$((PASS + FAIL))
echo "Results: $PASS/$TOTAL passed, $FAIL failed"
echo "========================================"

if [[ $FAIL -gt 0 ]]; then
    exit 1
fi
exit 0
