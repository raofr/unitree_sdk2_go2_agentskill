# GO2 Agent Tool Skill (Standalone Repo)

## Source

- https://github.com/raofr/go2_agent_tool

## Install

Option A (uv, recommended):

```bash
cd <tool_repo_dir>
uv venv .venv
source .venv/bin/activate
uv pip install -r requirements.txt
```

Option B (venv):

```bash
cd <tool_repo_dir>
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -U pip
python -m pip install -r requirements.txt
```

Option C (Node.js, no Python required):

```bash
cd <tool_repo_dir>/node
npm install
```

## OpenClaw Tool Command

Use this as command template in OpenClaw tool config:

```bash
python -m go2_agent_tool.cli --endpoint 192.168.51.213:50051 <subcommand ...>
```

Node.js template:

```bash
node <tool_repo_dir>/node/src/cli.js --endpoint 192.168.51.213:50051 <subcommand ...>
```

## Default Execution Policy

For non-parallel tasks:

1. force-close-owner --owner openclaw
2. open-session --owner openclaw --session-name <task_name>
3. action --session-id <id> --action <ACTION_...>
4. close-session --session-id <id>

For parallel tasks:

- open-session with --parallel
- do not force-close-owner

## Service Health Gate

Before first action, always run status:

```bash
python -m go2_agent_tool.cli --endpoint 192.168.51.213:50051 status
```

If status RPC fails, run dock-side service recovery from main GO2 skill at openclaw/skills/go2_grpc_skill.md.

## ActionName Full Enum List

All supported values for `--action`:

- ACTION_DAMP
- ACTION_BALANCE_STAND
- ACTION_STOP_MOVE
- ACTION_STAND_UP
- ACTION_STAND_DOWN
- ACTION_RECOVERY_STAND
- ACTION_EULER
- ACTION_MOVE
- ACTION_SIT
- ACTION_RISE_SIT
- ACTION_SPEED_LEVEL
- ACTION_HELLO
- ACTION_STRETCH
- ACTION_SWITCH_JOYSTICK
- ACTION_CONTENT
- ACTION_HEART
- ACTION_POSE
- ACTION_SCRAPE
- ACTION_FRONT_FLIP
- ACTION_FRONT_JUMP
- ACTION_FRONT_POUNCE
- ACTION_DANCE1
- ACTION_DANCE2
- ACTION_LEFT_FLIP
- ACTION_BACK_FLIP
- ACTION_HAND_STAND
- ACTION_FREE_WALK
- ACTION_FREE_BOUND
- ACTION_FREE_JUMP
- ACTION_FREE_AVOID
- ACTION_CLASSIC_WALK
- ACTION_WALK_UPRIGHT
- ACTION_CROSS_STEP
- ACTION_AUTO_RECOVER_SET
- ACTION_AUTO_RECOVER_GET
- ACTION_STATIC_WALK
- ACTION_TROT_RUN
- ACTION_ECONOMIC_GAIT
- ACTION_SWITCH_AVOID_MODE

## Action Call Patterns

Base template:

```bash
python -m go2_agent_tool.cli --endpoint 192.168.51.213:50051 action --session-id <id> --action <ACTION_NAME>
```

### 1) No extra args actions

Use base template directly for these actions:

- ACTION_DAMP
- ACTION_BALANCE_STAND
- ACTION_STOP_MOVE
- ACTION_STAND_UP
- ACTION_STAND_DOWN
- ACTION_RECOVERY_STAND
- ACTION_SIT
- ACTION_RISE_SIT
- ACTION_HELLO
- ACTION_STRETCH
- ACTION_CONTENT
- ACTION_HEART
- ACTION_POSE
- ACTION_SCRAPE
- ACTION_FRONT_FLIP
- ACTION_FRONT_JUMP
- ACTION_FRONT_POUNCE
- ACTION_DANCE1
- ACTION_DANCE2
- ACTION_LEFT_FLIP
- ACTION_BACK_FLIP
- ACTION_FREE_WALK
- ACTION_FREE_BOUND
- ACTION_FREE_JUMP
- ACTION_FREE_AVOID
- ACTION_CLASSIC_WALK
- ACTION_WALK_UPRIGHT
- ACTION_CROSS_STEP
- ACTION_AUTO_RECOVER_GET
- ACTION_STATIC_WALK
- ACTION_TROT_RUN
- ACTION_ECONOMIC_GAIT

### 2) Velocity actions

For `ACTION_MOVE`, set velocity args:

```bash
python -m go2_agent_tool.cli --endpoint 192.168.51.213:50051 action --session-id <id> --action ACTION_MOVE --vx <mps> --vy <mps> --vyaw <radps>
```

### 3) Euler actions

For `ACTION_EULER`, set roll/pitch/yaw:

```bash
python -m go2_agent_tool.cli --endpoint 192.168.51.213:50051 action --session-id <id> --action ACTION_EULER --roll <rad> --pitch <rad> --yaw <rad>
```

### 4) Integer level actions

For `ACTION_SPEED_LEVEL`, set `--level`:

```bash
python -m go2_agent_tool.cli --endpoint 192.168.51.213:50051 action --session-id <id> --action ACTION_SPEED_LEVEL --level <int>
```

### 5) Bool flag actions

For these actions, pass `--flag` when you need true:

- ACTION_SWITCH_JOYSTICK
- ACTION_HAND_STAND
- ACTION_AUTO_RECOVER_SET
- ACTION_SWITCH_AVOID_MODE

Example:

```bash
python -m go2_agent_tool.cli --endpoint 192.168.51.213:50051 action --session-id <id> --action ACTION_SWITCH_AVOID_MODE --flag
```

## Action Parameter JSON Mapping

Use this machine-readable mapping for tool/agent constraints:

- `go2_agent_tool/action_schema.json`

Constraint highlights:

- Always required: `session_id`, `action`
- `ACTION_MOVE` requires: `vx`, `vy`, `vyaw`
- `ACTION_EULER` requires: `roll`, `pitch`, `yaw`
- `ACTION_SPEED_LEVEL` requires: `level`
- `flag` optional only for:
	- `ACTION_SWITCH_JOYSTICK`, `ACTION_HAND_STAND`, `ACTION_AUTO_RECOVER_SET`, `ACTION_SWITCH_AVOID_MODE`
