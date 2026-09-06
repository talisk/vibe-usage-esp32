"""Local stdio protocol fixture. Never contacts a model or reads real auth."""
import json
import os
import subprocess
import signal
import sys
import time


def send(value):
    print(json.dumps(value), flush=True)


def event(method, params):
    send({"method": method, "params": params})


for line in sys.stdin:
    request = json.loads(line)
    method, params = request.get("method"), request.get("params", {})
    ident = request.get("id")
    if method == "initialize":
        result = {}
    elif method == "account/read":
        result = {"account": {"type": os.environ.get("VIBE_TEST_AUTH", "chatgpt")}}
    elif method == "config/read":
        result = {"config": {"model_provider": "openai", "mcp_servers": {"fixture": {}}}}
    elif method == "mcpServerStatus/list":
        result = {"data": [{"name": "fixture", "runtimeStatus": "disabled"}], "nextCursor": None}
    elif method == "thread/start":
        assert params["ephemeral"] is True
        assert params["environments"] == params["dynamicTools"] == params["selectedCapabilityRoots"] == []
        assert params["config"]["mcp_servers"]["fixture"]["enabled"] is False
        assert params["sandbox"] == "read-only" and params["approvalPolicy"] == "never"
        result = {"thread": {"id": "thread-fixture"}}
    elif method == "turn/start":
        event("turn/started", {"threadId": "thread-fixture", "turn": {"id": "turn-fixture"}})
        result = {"turn": {"id": "turn-fixture"}}
        send({"id": ident, "result": result})
        output = {"action": "add", "title": "Synthetic task", "id": None, "delay_seconds": 0, "repeat_seconds": 0}
        event("item/completed", {"threadId": "thread-fixture", "turnId": "turn-fixture",
                                "item": {"type": "agentMessage", "phase": "final_answer", "text": json.dumps(output)}})
        event("turn/completed", {"threadId": "thread-fixture", "turn": {"id": "turn-fixture", "status": "completed"}})
        continue
    elif method == "test/inbound":
        send({"id": "host-request", "method": "item/tool/call", "params": {}})
        continue
    elif method == "test/unexpected":
        event("turn/started", {"threadId": "voice-thread", "turn": {"id": "unexpected"}})
        continue
    elif method == "test/child":
        child = subprocess.Popen([sys.executable, "-c", "import time;time.sleep(60)"])
        result = {"pid": child.pid}
    elif method == "test/error":
        send({"id": ident, "error": {"code": -32000, "message": "401 token PRIVATE_CANARY"}})
        continue
    elif method == "test/sleep":
        time.sleep(60)
        continue
    elif method == "test/ignore_term":
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        result = {}
    elif method == "test/environment":
        result = {"api_key_present": any(os.environ.get(k) for k in
                  ("OPENAI_API_KEY", "CODEX_API_KEY", "CODEX_ACCESS_TOKEN")), "cwd": os.getcwd()}
    elif method in ("initialized", "turn/interrupt") or method is None:
        continue
    else:
        result = {}
    if ident is not None:
        send({"id": ident, "result": result})
