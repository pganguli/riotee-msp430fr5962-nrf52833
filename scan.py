#!/usr/bin/env python3
"""Scan for Riotee BLE advertisements and print the incrementing counter."""

import socket
import struct

import dbus
import dbus.mainloop.glib
from gi.repository import GLib

# ── Configuration ────────────────────────────────────────────────────────────

TARGET_NAME    = "RIOTEE"
MNF_ID_NORDIC  = 0x0059   # Nordic Semiconductor BT SIG company ID
BLUEZ_SERVICE  = "org.bluez"
ADAPTER_PATH   = "/org/bluez/hci0"

# Continuous scan: window == interval → 100% duty cycle.
SCAN_INTERVAL_MS = 10
SCAN_WINDOW_MS   = 10

# ── HCI constants (Linux-only, not in socket's type stubs) ───────────────────

AF_BLUETOOTH: int = getattr(socket, "AF_BLUETOOTH")
BTPROTO_HCI:  int = getattr(socket, "BTPROTO_HCI")

# HCI LE_Set_Scan_Parameters: OGF=0x08, OCF=0x000B
HCI_LE_SET_SCAN_PARAMS = 0x200B

# ── HCI helpers ──────────────────────────────────────────────────────────────

def set_scan_parameters(
    interval_ms: float = SCAN_INTERVAL_MS,
    window_ms:   float = SCAN_WINDOW_MS,
    hci_idx:     int   = 0,
) -> None:
    """Push LE scan interval/window directly via HCI, bypassing BlueZ defaults.

    Values are converted from ms to HCI units of 0.625 ms.
    Requires CAP_NET_RAW (run with sudo or grant it to the Python binary).
    """
    interval = round(interval_ms / 0.625)
    window   = round(window_ms   / 0.625)

    params = struct.pack(
        "<BHHBB",
        0x00,      # scan type: passive
        interval,
        window,
        0x00,      # own address type: public
        0x00,      # filter policy: accept all
    )
    pkt = struct.pack("<BHB", 0x01, HCI_LE_SET_SCAN_PARAMS, len(params)) + params

    with socket.socket(AF_BLUETOOTH, socket.SOCK_RAW, BTPROTO_HCI) as sock:
        sock.bind((hci_idx,))
        sock.send(pkt)

# ── Advertisement callback ───────────────────────────────────────────────────

class CounterScanner:
    """Tracks the last seen counter value and reports gaps."""

    def __init__(self) -> None:
        self._last: int | None = None

    def on_properties_changed(
        self,
        interface: str,
        changed:   dict,
        *_,
        **__,
    ) -> None:
        if interface != "org.bluez.Device1":
            return
        if "ManufacturerData" not in changed:
            return

        raw = changed["ManufacturerData"].get(MNF_ID_NORDIC)
        if raw is None or len(raw) < 4:
            return

        counter: int = struct.unpack_from("<I", bytes(raw))[0]
        if counter == self._last:
            return  # duplicate advert for the same value

        if self._last is not None and counter != self._last + 1:
            missed = counter - self._last - 1
            print(f"  !! missed {missed} value(s): {self._last + 1}–{counter - 1}")

        rssi = changed.get("RSSI")
        rssi_str = f"  rssi={int(rssi)} dBm" if rssi is not None else ""
        print(f"counter={counter}{rssi_str}")
        self._last = counter

# ── Entry point ──────────────────────────────────────────────────────────────

def main() -> None:
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()

    set_scan_parameters()

    adapter = dbus.Interface(
        bus.get_object(BLUEZ_SERVICE, ADAPTER_PATH),
        "org.bluez.Adapter1",
    )
    adapter.SetDiscoveryFilter({
        "Transport":     dbus.String("le"),
        "DuplicateData": dbus.Boolean(True),
    })

    scanner = CounterScanner()
    bus.add_signal_receiver(
        scanner.on_properties_changed,
        dbus_interface="org.freedesktop.DBus.Properties",
        signal_name="PropertiesChanged",
        arg0="org.bluez.Device1",
        path_keyword="path",
    )

    adapter.StartDiscovery()
    print(
        f"Scanning for '{TARGET_NAME}'"
        f" ({SCAN_INTERVAL_MS} ms interval, DuplicateData=True)"
        f"... Press Ctrl+C to stop."
    )

    loop = GLib.MainLoop()
    try:
        loop.run()
    except KeyboardInterrupt:
        print("\nScan stopped.")
        adapter.StopDiscovery()


if __name__ == "__main__":
    main()
