#!/usr/bin/env python3
"""Libvirt VM lifecycle monitor for etmem management.

This service listens for libvirt VM start/stop events and manages
etmem configurations to enable memory tiering for QEMU/KVM VMs.
"""

import logging
import os
import re
import signal
import subprocess
import sys
import threading

import libvirt

ETMEM_DIR = "/etc/etmem"
KVM_TEMPLATE = os.path.join(ETMEM_DIR, "kvm.yaml")
QEMU_PID_DIR = "/var/run/libvirt/qemu"
ETMEM_SOCKET = "etmemd_socket"
LIBVIRT_URI = "qemu:///system"

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(message)s",
)
logger = logging.getLogger(__name__)


def _is_valid_vm_name(vm_name):
    """Return True if vm_name is safe to use in file paths.

    Rejects names containing path separators, traversal sequences,
    or characters that are invalid in file names.
    """
    return bool(vm_name) and re.match(r'^[A-Za-z0-9._-]+$', vm_name) is not None


def get_vm_pid(vm_name):
    """Read the QEMU process PID from the libvirt PID file.

    Returns the integer PID, or None on failure.
    """
    pid_file = os.path.join(QEMU_PID_DIR, f"{vm_name}.pid")
    try:
        with open(pid_file, "r", encoding="utf-8") as f:
            return int(f.read().strip())
    except (OSError, ValueError) as exc:
        logger.error("Failed to read PID for VM %s: %s", vm_name, exc)
        return None


def is_mem_locked(pid):
    """Return True if 'mem-lock=on' is found in the QEMU process cmdline.

    Full memory reservation VMs must be skipped by etmem.
    """
    cmdline_path = f"/proc/{pid}/cmdline"
    try:
        with open(cmdline_path, "rb") as f:
            cmdline = f.read().decode("utf-8", errors="replace")
        return "mem-lock=on" in cmdline
    except OSError as exc:
        logger.error("Failed to read cmdline for PID %d: %s", pid, exc)
        return False


def get_vm_config_path(vm_name):
    """Return the path of the VM-specific etmem configuration file."""
    return os.path.join(ETMEM_DIR, f"{vm_name}.yaml")


def _render_config(template_content, vm_name, pid):
    """Substitute 'vm_name' placeholder and set the 'value' to the given PID."""
    content = template_content.replace("vm_name", vm_name)
    lines = []
    for line in content.splitlines():
        if line.lstrip().startswith("value="):
            line = f"value={pid}"
        lines.append(line)
    return "\n".join(lines) + "\n"


def create_vm_config(vm_name, pid):
    """Create an etmem config file for the VM from the KVM template.

    The file is written with mode 400 (read-only for owner).
    Returns the config path on success, or None on failure.
    """
    config_path = get_vm_config_path(vm_name)
    try:
        with open(KVM_TEMPLATE, "r", encoding="utf-8") as f:
            template_content = f.read()
        content = _render_config(template_content, vm_name, pid)
        with open(config_path, "w", encoding="utf-8") as f:
            f.write(content)
        os.chmod(config_path, 0o400)
        logger.info("Created etmem config %s (VM=%s PID=%d)", config_path, vm_name, pid)
        return config_path
    except OSError as exc:
        logger.error("Failed to create etmem config for VM %s: %s", vm_name, exc)
        return None


def run_etmem_command(args):
    """Execute an etmem sub-command and return True on success."""
    cmd = ["etmem"] + args
    cmd_str = " ".join(cmd)
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        if result.returncode != 0:
            logger.error("etmem failed [%s]: %s", cmd_str, result.stderr.strip())
            return False
        logger.info("etmem succeeded: %s", cmd_str)
        return True
    except (subprocess.TimeoutExpired, OSError) as exc:
        logger.error("etmem error [%s]: %s", cmd_str, exc)
        return False


def enable_vm_etmem(vm_name, config_path):
    """Add the etmem object and start the project for a VM."""
    add_ok = run_etmem_command(["obj", "add", "-f", config_path, "-s", ETMEM_SOCKET])
    if not add_ok:
        return False
    return run_etmem_command(["project", "start", "-n", vm_name, "-s", ETMEM_SOCKET])


def handle_vm_started(vm_name):
    """Handle a VM start event: validate, create config, and enable etmem."""
    if not _is_valid_vm_name(vm_name):
        logger.error("VM name '%s' contains invalid characters, skipping", vm_name)
        return
    pid = get_vm_pid(vm_name)
    if pid is None:
        return
    if is_mem_locked(pid):
        logger.info("VM %s uses mem-lock=on (full memory reservation), skipping", vm_name)
        return
    config_path = create_vm_config(vm_name, pid)
    if config_path:
        enable_vm_etmem(vm_name, config_path)


def handle_vm_stopped(vm_name):
    """Handle a VM stop event: remove the etmem object and delete the config."""
    if not _is_valid_vm_name(vm_name):
        logger.error("VM name '%s' contains invalid characters, skipping", vm_name)
        return
    config_path = get_vm_config_path(vm_name)
    if not os.path.exists(config_path):
        logger.info("No etmem config for VM %s, nothing to clean up", vm_name)
        return
    run_etmem_command(["obj", "del", "-f", config_path, "-s", ETMEM_SOCKET])
    try:
        os.remove(config_path)
        logger.info("Removed etmem config %s", config_path)
    except OSError as exc:
        logger.error("Failed to remove config %s: %s", config_path, exc)


def lifecycle_callback(conn, domain, event, detail, opaque):  # pylint: disable=unused-argument
    """Libvirt domain lifecycle event callback."""
    vm_name = domain.name()
    logger.info("Lifecycle event: VM=%s event=%d detail=%d", vm_name, event, detail)
    if event == libvirt.VIR_DOMAIN_EVENT_STARTED:
        handle_vm_started(vm_name)
    elif event in (libvirt.VIR_DOMAIN_EVENT_STOPPED, libvirt.VIR_DOMAIN_EVENT_SHUTDOWN):
        handle_vm_stopped(vm_name)


def register_lifecycle_event(conn):
    """Register the VIR_DOMAIN_EVENT_ID_LIFECYCLE callback with libvirt."""
    conn.domainEventRegisterAny(
        None,
        libvirt.VIR_DOMAIN_EVENT_ID_LIFECYCLE,
        lifecycle_callback,
        None,
    )
    conn.setKeepAlive(5, 3)
    logger.info("Registered VIR_DOMAIN_EVENT_ID_LIFECYCLE callback")


def setup_signal_handlers(stop_event):
    """Register SIGTERM and SIGINT handlers to trigger graceful shutdown."""
    def handler(signum, frame):  # pylint: disable=unused-argument
        logger.info("Received signal %d, initiating shutdown", signum)
        stop_event.set()

    signal.signal(signal.SIGTERM, handler)
    signal.signal(signal.SIGINT, handler)


def run_event_loop(stop_event):
    """Run the libvirt default event loop until the stop event is set."""
    while not stop_event.is_set():
        libvirt.virEventRunDefaultImpl()


def main():
    """Connect to libvirt, register event listener, and run the event loop."""
    libvirt.virEventRegisterDefaultImpl()
    conn = libvirt.open(LIBVIRT_URI)
    if conn is None:
        logger.error(
            "Failed to connect to libvirt at %s. "
            "Ensure libvirtd is running and this process has the required permissions.",
            LIBVIRT_URI,
        )
        sys.exit(1)

    stop_event = threading.Event()
    setup_signal_handlers(stop_event)
    register_lifecycle_event(conn)
    logger.info("VM lifecycle monitor started, listening for events...")

    try:
        run_event_loop(stop_event)
    finally:
        conn.close()
        logger.info("Disconnected from libvirt. Service stopped.")


if __name__ == "__main__":
    main()
