#!/usr/bin/env python3
"""
Claude Code Arduino Bridge
Claude Code hook 이벤트를 Arduino OLED에 표시하고,
PreToolUse confirm 모드에서 버튼/키보드 입력을 대기한 뒤 진행.
아두이노 미연결 시 키보드 Enter 대기 모드로 자동 폴백.
"""

import sys
import json
import time
import os
import threading
import argparse
from typing import Optional

try:
    import serial
    import serial.tools.list_ports
    SERIAL_AVAILABLE = True
except ImportError:
    SERIAL_AVAILABLE = False

# 시리얼 포트 자동 탐색 키워드 (Windows COM 포트 포함)
PORT_KEYWORDS = ["CH340", "Arduino", "USB Serial", "usbserial", "ttyUSB", "ttyACM"]
BAUD_RATE = 9600
TIMEOUT_SECONDS = 30

# 훅별 기본 동작 모드
HOOK_MODE = {
    "PreToolUse": "CONFIRM",
    "PostToolUse": "NOTIFY",
    "Notification": "NOTIFY",
    "Stop": "NOTIFY",
}


# ──────────────────────────────────────────────
# 시리얼 연결
# ──────────────────────────────────────────────

def find_arduino_port() -> Optional[str]:
    """CH340, Arduino, USB Serial 키워드로 포트 자동 탐색"""
    if not SERIAL_AVAILABLE:
        return None
    try:
        for port in serial.tools.list_ports.comports():
            haystack = f"{port.description} {port.manufacturer or ''} {port.hwid or ''}"
            if any(kw.lower() in haystack.lower() for kw in PORT_KEYWORDS):
                return port.device
    except Exception:
        pass
    return None


def connect_arduino(port: str) -> Optional["serial.Serial"]:
    """Arduino 시리얼 연결. 실패 시 None 반환."""
    try:
        ser = serial.Serial(port, BAUD_RATE, timeout=0.1)
        time.sleep(2)  # Arduino 리셋 대기
        ser.reset_input_buffer()
        return ser
    except Exception:
        return None


def send_command(ser: "serial.Serial", command: str) -> None:
    """Arduino에 명령 전송. 오류는 무시."""
    try:
        ser.write(f"{command}\n".encode("utf-8"))
    except Exception:
        pass


# ──────────────────────────────────────────────
# confirm 모드 입력 대기
# ──────────────────────────────────────────────

def _wait_arduino_button(ser: "serial.Serial", done: threading.Event) -> None:
    """Arduino 버튼 입력 대기 (스레드용)"""
    while not done.is_set():
        try:
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if line == "OK":
                done.set()
                return
        except Exception:
            return


def _wait_keyboard(done: threading.Event) -> None:
    """
    키보드 Enter 대기 (스레드용).
    stdin은 이미 JSON 읽기로 소비되었으므로 터미널을 직접 열어 읽음.
    """
    try:
        if os.name == "nt":
            # Windows: msvcrt로 키보드 직접 읽기
            import msvcrt
            while not done.is_set():
                if msvcrt.kbhit():
                    key = msvcrt.getwch()
                    if key in ("\r", "\n", " "):
                        done.set()
                        return
                time.sleep(0.05)
        else:
            # Linux/macOS: /dev/tty 직접 열기
            with open("/dev/tty", "r") as tty:
                tty.readline()
            done.set()
    except Exception:
        # 터미널 접근 불가 시 타임아웃에 맡김
        pass


def run_confirm(ser: Optional["serial.Serial"]) -> None:
    """버튼 / 키보드 Enter / 30초 타임아웃 중 먼저 오는 것으로 진행"""
    done = threading.Event()

    threads = []

    if ser:
        t = threading.Thread(target=_wait_arduino_button, args=(ser, done), daemon=True)
        threads.append(t)
        t.start()

    t = threading.Thread(target=_wait_keyboard, args=(done,), daemon=True)
    threads.append(t)
    t.start()

    done.wait(timeout=TIMEOUT_SECONDS)
    # done 이후 데몬 스레드들은 프로세스 종료 시 자동 정리됨


# ──────────────────────────────────────────────
# 메시지 생성
# ──────────────────────────────────────────────

def build_message(hook_type: str, event_data: dict) -> tuple[str, str]:
    """
    훅 이벤트 데이터로부터 (커맨드타입, OLED 메시지) 생성.
    커맨드타입은 CONFIRM 또는 NOTIFY.
    """
    tool_name: str = event_data.get("tool_name", "")
    tool_input: dict = event_data.get("tool_input", {})

    if hook_type == "PreToolUse":
        if tool_name == "Bash":
            cmd = str(tool_input.get("command", "")).strip()
            preview = cmd[:35] + ("..." if len(cmd) > 35 else "")
            return "CONFIRM", f"Should I run this?\n$ {preview}"

        if tool_name in ("Write", "Edit", "MultiEdit"):
            path = str(tool_input.get("file_path", tool_input.get("path", "??")))
            short = path if len(path) <= 28 else "..." + path[-25:]
            return "CONFIRM", f"Shall I edit this?\n{short}"

        if tool_name:
            return "CONFIRM", f"Should I use\n{tool_name}?"

        return "CONFIRM", "Should I proceed?"

    if hook_type == "PostToolUse":
        if tool_name == "Bash":
            return "NOTIFY", "Done! Command\nfinished."
        if tool_name in ("Write", "Edit", "MultiEdit"):
            return "NOTIFY", "Done! File saved.\nLooks good."
        if tool_name:
            return "NOTIFY", f"Done!\n{tool_name} finished."
        return "NOTIFY", "Done!"

    if hook_type == "Notification":
        msg = str(event_data.get("message", "Hey, I need\nyour attention."))
        return "NOTIFY", msg[:55]

    if hook_type == "Stop":
        return "NOTIFY", "All done!\nThat wasn't so bad."

    return "NOTIFY", "Something happened!"


# ──────────────────────────────────────────────
# 메인
# ──────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(description="Claude Code Arduino bridge")
    parser.add_argument(
        "hook_type",
        choices=["PreToolUse", "PostToolUse", "Notification", "Stop"],
        help="Claude Code hook 이벤트 타입",
    )
    args = parser.parse_args()

    # Claude Code는 stdin으로 JSON 이벤트 데이터를 전달
    try:
        raw = sys.stdin.read()
        event_data: dict = json.loads(raw) if raw.strip() else {}
    except Exception:
        event_data = {}

    try:
        cmd_type, message = build_message(args.hook_type, event_data)

        # Arduino 연결 (실패해도 조용히 폴백)
        ser: Optional[serial.Serial] = None
        if SERIAL_AVAILABLE:
            port = find_arduino_port()
            if port:
                ser = connect_arduino(port)

        if ser:
            send_command(ser, f"{cmd_type}:{message}")

        if cmd_type == "CONFIRM":
            # 아두이노 없으면 키보드 Enter만 대기
            run_confirm(ser)
        else:
            # NOTIFY: 3초 표시 후 화면 정리
            time.sleep(3)
            if ser:
                send_command(ser, "CLEAR")

        if ser:
            try:
                ser.close()
            except Exception:
                pass

    except Exception:
        # 어떤 예외도 Claude Code 작업을 막으면 안 됨 — 조용히 종료
        pass


if __name__ == "__main__":
    main()
