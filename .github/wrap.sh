#!/bin/bash
# Runs a workflow step's script like "bash -e", and if it fails also writes the important lines of its output into an error
# annotation (error / FAIL lines first, then the last lines), so the reason can be read on the run's summary page and through the GitHub API.
bash -eo pipefail "$1" 2>&1 | tee /tmp/step.log
rc=${PIPESTATUS[0]}
if [ "$rc" -ne 0 ]; then
  { grep -E "error|FAIL|failed|checks,|MISSED|Assertion|undefined|not found" /tmp/step.log | grep -v "warning:" | head -n 30; echo "---- end of log ----"; tail -n 25 /tmp/step.log; } | cut -c1-300 | sed 's/%/%25/g' | awk '{printf "%s%%0A", $0}' > /tmp/msg.txt
  echo "::error title=Step failed (exit code $rc)::$(cat /tmp/msg.txt)"
fi
exit "$rc"
