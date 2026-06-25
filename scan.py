#!/usr/bin/env python3
"""
Riotee Bonito BLE monitor.

Listens for BLE advertisements from the Riotee board running the Bonito
firmware and prints each round's sequence number, Bonito connection interval,
and application payload.

Also verifies the node's advertised CI against a reference Bonito model running
on the laptop (replaying the same synthetic charging-time trace as the nRF52).
Gaps between sequence numbers indicate BLE packet loss; the verifier catches up
through missed rounds automatically.

Usage:
    sudo ./scan.py
"""

import argparse
import datetime
import math
import socket
import struct

import dbus
import dbus.mainloop.glib
from gi.repository import GLib

# ── Configuration ──────────────────────────────────────────────────────────────

MNF_ID_NORDIC = 0x0059  # Nordic Semiconductor BT SIG company ID
BLUEZ_SERVICE = "org.bluez"
ADAPTER_PATH = "/org/bluez/hci0"

# Continuous scan: window == interval → 100% duty cycle.
SCAN_INTERVAL_MS = 10
SCAN_WINDOW_MS = 10

# Bonito parameters — must match nrf52833/src/bonito/bonito.h and main.c.
BONITO_TARGET_PROB = 0.99
BONITO_INIT_MEAN = 1.0  # seconds
BONITO_INIT_VAR = 0.25  # seconds^2
BONITO_ETA = 0.01  # SGD learning rate

# CI verification tolerance: single-precision Acklam vs double scipy → ~1e-2.
CI_REL_TOL = 0.05  # 5% relative tolerance (generous for float32 vs float64)

# Advert payload layout (must match nrf52833/src/bonito_payload.h):
#   [seq:u16][model_type:u8][app_len:u8][mean:f32][var:f32][app:4 bytes]
# Budget: ble.c payload[31] = 3(flags)+2(name-hdr)+6("RIOTEE")+4(mfr-hdr)+data
# → data ≤ 16 bytes = BONITO_ADV_HDR_LEN(12) + BONITO_ADV_APP_MAX(4).
BONITO_MODEL_NORMAL = 0x01
BONITO_ADV_HDR_LEN = 12   # seq(2)+model_type(1)+app_len(1)+mean(4)+var(4)
BONITO_ADV_APP_MAX = 4
BONITO_ADV_TOTAL = BONITO_ADV_HDR_LEN + BONITO_ADV_APP_MAX  # = 16

# ── Charging-time trace (shared with nrf52833/src/charge_trace.h) ─────────────
#
# Generated with:
#   numpy.random.seed(42)
#   t = 1.0 + 0.3*sin(2*pi*i/50) + normal(0, 0.05)  for i in 0..255
#   t = max(t, 0.1)

CHARGE_TRACE = [
    1.024836,
    1.030687,
    1.106991,
    1.186589,
    1.132818,
    1.164629,
    1.284325,
    1.269526,
    1.229825,
    1.298576,
    1.262146,
    1.271400,
    1.311506,
    1.203744,
    1.208440,
    1.257203,
    1.220807,
    1.269011,
    1.185753,
    1.134749,
    1.249618,
    1.133237,
    1.113814,
    1.003370,
    1.010381,
    1.005546,
    0.904850,
    0.944178,
    0.859531,
    0.840889,
    0.793579,
    0.887250,
    0.768171,
    0.693816,
    0.769679,
    0.653641,
    0.715757,
    0.602608,
    0.634183,
    0.715157,
    0.751606,
    0.737120,
    0.740919,
    0.753791,
    0.720710,
    0.787672,
    0.832442,
    0.942419,
    0.942574,
    0.874248,
    1.016204,
    1.018346,
    1.040761,
    1.141021,
    1.196076,
    1.222900,
    1.163403,
    1.215693,
    1.269862,
    1.320225,
    1.261358,
    1.285403,
    1.244091,
    1.239598,
    1.335312,
    1.353129,
    1.267848,
    1.303475,
    1.249236,
    1.173108,
    1.194405,
    1.221428,
    1.108646,
    1.152839,
    0.906613,
    1.041095,
    0.966752,
    0.910443,
    0.894151,
    0.756095,
    0.812681,
    0.812491,
    0.842741,
    0.720788,
    0.688127,
    0.689595,
    0.751084,
    0.717030,
    0.674104,
    0.730977,
    0.719537,
    0.776984,
    0.711599,
    0.752463,
    0.775030,
    0.750489,
    0.870280,
    0.902615,
    0.925649,
    0.950671,
    0.929231,
    1.016568,
    1.057471,
    1.070324,
    1.136462,
    1.196538,
    1.299673,
    1.239883,
    1.266176,
    1.267726,
    1.189378,
    1.293360,
    1.302420,
    1.422570,
    1.285068,
    1.300394,
    1.269713,
    1.194864,
    1.288295,
    1.242961,
    1.215887,
    1.099057,
    1.180577,
    1.004514,
    1.066943,
    1.109523,
    0.912873,
    0.897078,
    0.894545,
    0.830300,
    0.746131,
    0.798064,
    0.715731,
    0.770381,
    0.682581,
    0.792180,
    0.666151,
    0.684489,
    0.741268,
    0.643771,
    0.726056,
    0.793909,
    0.666327,
    0.778078,
    0.807630,
    0.862756,
    0.793626,
    0.823540,
    0.951490,
    0.977249,
    1.012525,
    1.054922,
    1.040606,
    1.122050,
    1.159180,
    1.140618,
    1.298653,
    1.254846,
    1.193733,
    1.304276,
    1.236583,
    1.334040,
    1.357338,
    1.258374,
    1.342855,
    1.305956,
    1.312551,
    1.348138,
    1.218885,
    1.167677,
    1.131860,
    1.103736,
    1.106582,
    1.091665,
    1.051435,
    1.041359,
    0.963050,
    0.998070,
    0.876330,
    0.991482,
    0.854948,
    0.751778,
    0.715301,
    0.770825,
    0.717379,
    0.750383,
    0.728976,
    0.696951,
    0.658252,
    0.629571,
    0.692357,
    0.771372,
    0.757406,
    0.706559,
    0.803295,
    0.842930,
    0.811281,
    0.897249,
    0.928303,
    0.905252,
    1.017889,
    1.065639,
    1.128760,
    1.163127,
    1.075643,
    1.129444,
    1.231116,
    1.256843,
    1.279051,
    1.464085,
    1.313861,
    1.351464,
    1.347108,
    1.331978,
    1.278923,
    1.323265,
    1.232807,
    1.241457,
    1.206886,
    1.209458,
    1.292069,
    1.051163,
    1.144750,
    0.993971,
    1.014003,
    1.054448,
    0.965614,
    0.871506,
    0.853797,
    0.889454,
    0.787146,
    0.805459,
    0.771125,
    0.714122,
    0.835749,
    0.746379,
    0.604057,
    0.709915,
    0.667503,
    0.747935,
    0.675057,
    0.722815,
    0.771951,
    0.812134,
    0.734621,
    0.806939,
    0.831727,
    0.856896,
    1.013666,
    0.982649,
    0.936956,
    1.083493,
    1.180715,
    1.162061,
    1.068558,
    1.152124,
]

# ── HCI constants (Linux-only, not in socket's type stubs) ────────────────────

AF_BLUETOOTH: int = getattr(socket, "AF_BLUETOOTH")
BTPROTO_HCI: int = getattr(socket, "BTPROTO_HCI")
HCI_LE_SET_SCAN_PARAMS = 0x200B  # OGF=0x08, OCF=0x000B

# ── HCI helpers ───────────────────────────────────────────────────────────────


def set_scan_parameters(
    interval_ms: float = SCAN_INTERVAL_MS,
    window_ms: float = SCAN_WINDOW_MS,
    hci_idx: int = 0,
) -> None:
    """Push LE scan interval/window directly via HCI, bypassing BlueZ defaults.

    Values are converted from ms to HCI units of 0.625 ms.
    Requires CAP_NET_RAW (run with sudo or grant it to the Python binary).
    """
    interval = round(interval_ms / 0.625)
    window = round(window_ms / 0.625)

    params = struct.pack(
        "<BHHBB",
        0x00,  # scan type: passive
        interval,
        window,
        0x00,  # own address type: public
        0x00,  # filter policy: accept all
    )
    pkt = struct.pack("<BHB", 0x01, HCI_LE_SET_SCAN_PARAMS, len(params)) + params

    with socket.socket(AF_BLUETOOTH, socket.SOCK_RAW, BTPROTO_HCI) as sock:
        sock.bind((hci_idx,))
        sock.send(pkt)


# ── Bonito reference model (mirrors NormalDistribution in distributions.py) ───


def _probit(p: float) -> float:
    """Inverse standard normal CDF via scipy (double-precision reference)."""
    from scipy.stats import norm

    return float(norm.ppf(p))


class BonitoRef:
    """
    Reference Bonito NormalDistribution (double-precision, scipy-backed).
    Replays the same SGD as the nRF52 using the shared charging-time trace.
    Used to verify the node's advertised CI.
    """

    def __init__(self) -> None:
        self._mean = BONITO_INIT_MEAN
        self._var = BONITO_INIT_VAR
        self._eta = BONITO_ETA
        self._ref_seq = 0  # how many SGD updates have been applied

    def _ppf(self, p: float) -> float:
        return self._mean + math.sqrt(self._var) * _probit(p)

    def _sgd_update(self, x: float) -> None:
        diff = x - self._mean
        self._mean += self._eta * diff
        self._var += self._eta * (diff * diff - self._var)

    @property
    def mean(self) -> float:
        return self._mean

    @property
    def sigma(self) -> float:
        return math.sqrt(max(self._var, 0.0))

    def reference_ci_for_seq(self, seq: int) -> float:
        """
        Return the CI (seconds) the node should have computed for this seq,
        catching up through any missed rounds first.

        Order mirrors protocols.py:20-28:
          ci = ppf(current_dist)  →  yield ci  →  sgd_update(c)
        """
        # Catch up: apply SGD for all rounds we missed (ref_seq .. seq-1).
        while self._ref_seq < seq:
            c = CHARGE_TRACE[self._ref_seq % len(CHARGE_TRACE)]
            self._sgd_update(c)
            self._ref_seq += 1

        # CI for THIS round uses the model BEFORE the update for round `seq`.
        ci = self._ppf(BONITO_TARGET_PROB)

        # Now apply the SGD update for this round so the next call is correct.
        c = CHARGE_TRACE[seq % len(CHARGE_TRACE)]
        self._sgd_update(c)
        self._ref_seq = seq + 1

        return ci

    def ci_for_seq_peek(self, seq: int) -> float:
        """Return CI for seq without advancing the model (for missed-round display)."""
        # Temporarily replay up to seq without mutation.
        mean, var, ref_seq = self._mean, self._var, self._ref_seq
        while ref_seq < seq:
            c = CHARGE_TRACE[ref_seq % len(CHARGE_TRACE)]
            diff = c - mean
            mean += self._eta * diff
            var += self._eta * (diff * diff - var)
            ref_seq += 1
        return mean + math.sqrt(max(var, 0.0)) * _probit(BONITO_TARGET_PROB)


# ── Laptop-side Bonito peer (stable / always-on) ─────────────────────────────
#
# The laptop is modelled as a node with a constant charging time equal to its
# own CI — i.e. it always wakes up exactly on schedule.  Practically its
# distribution converges to a very tight Normal(mean≈CI, var≈0) and its ppf
# contribution to the joint CI is negligible, so the joint CI ≈ node CI.  We
# simulate it anyway so the debug output shows both sides of the protocol.


class LaptopPeer:
    """
    Bonito NormalDistribution for the stable laptop peer.

    The laptop is always-on, so its charging time each round is effectively 0
    (it never needs to wait for energy).  Feeding 0.0 into the SGD drives
    mean → 0 and var → 0, giving ppf(0.99) → 0.  The joint CI is therefore
    always dominated by the node, which is the correct Bonito behaviour for an
    always-on peer.
    """

    def __init__(self) -> None:
        self._mean = BONITO_INIT_MEAN
        self._var = BONITO_INIT_VAR
        self._eta = BONITO_ETA

    @property
    def mean(self) -> float:
        return self._mean

    @property
    def sigma(self) -> float:
        return math.sqrt(max(self._var, 0.0))

    def ppf(self, p: float) -> float:
        return self._mean + self.sigma * _probit(p)

    def update(self) -> None:
        """Always-on: charging time is 0 every round."""
        diff = 0.0 - self._mean
        self._mean += self._eta * diff
        self._var += self._eta * (diff * diff - self._var)

    def joint_ci(
        self, node_mean: float, node_sigma: float, p: float = BONITO_TARGET_PROB
    ) -> float:
        """
        Bisect for t where F_laptop(t) * F_node(t) = p.

        Both distributions are Normal; scipy.stats.norm.cdf gives the CDF.
        For an always-on laptop this converges to node.ppf(p) as the laptop's
        sigma → 0.
        """
        from scipy.stats import norm as _norm

        def _joint_cdf(t: float) -> float:
            lz = (t - self._mean) / max(self.sigma, 1e-9)
            nz = (t - node_mean) / max(node_sigma, 1e-9)
            return float(_norm.cdf(lz)) * float(_norm.cdf(nz))

        lo, hi = 0.0, 60.0
        for _ in range(48):
            mid = (lo + hi) / 2.0
            if _joint_cdf(mid) < p:
                lo = mid
            else:
                hi = mid
        return (lo + hi) / 2.0


# ── Advertisement callback ────────────────────────────────────────────────────


class BonitoScanner:
    """Decodes Bonito advertisements, verifies CI, detects gaps."""

    def __init__(self, real: bool = False) -> None:
        self._real = real  # True → skip CHARGE_TRACE CI verification
        self._last_seq: int | None = None
        self._ref = BonitoRef()
        self._laptop = LaptopPeer()
        self._rx = 0  # rounds received this session
        self._exp = 0  # rounds expected this session
        self._session: int = 0  # reboot counter
        self._round: int = 0  # round within current session
        self._session_start: datetime.datetime | None = None
        self._session_first_ci: int | None = None
        self._prev_ci_ms: int | None = None
        self._last_rx_time: datetime.datetime | None = None

    def _reset_session(self) -> None:
        self._ref = BonitoRef()
        self._laptop = LaptopPeer()  # laptop peer resets with each node session
        self._rx = 0
        self._exp = 0
        self._session += 1
        self._round = 0
        self._session_start = datetime.datetime.now()
        self._session_first_ci = None
        self._prev_ci_ms = None
        self._last_seq = None
        self._last_rx_time = None

    def on_properties_changed(
        self,
        interface: str,
        changed: dict,
        *_,
        **__,
    ) -> None:
        if interface != "org.bluez.Device1":
            return
        if "ManufacturerData" not in changed:
            return

        raw = changed["ManufacturerData"].get(MNF_ID_NORDIC)
        if raw is None or len(raw) < BONITO_ADV_HDR_LEN:
            return

        data = bytes(raw)
        # Decode model parameters from the wire (paper Fig. 12 format).
        # The CI is not transmitted — compute it locally from mean + var.
        seq, model_type, app_len, mean, var = struct.unpack_from("<HBBff", data, 0)
        if model_type != BONITO_MODEL_NORMAL:
            print(f"  ?? unknown model_type=0x{model_type:02x}, skipping")
            return
        node_sigma = math.sqrt(max(var, 0.0))
        node_ci_s = mean + node_sigma * _probit(BONITO_TARGET_PROB)
        node_ci_s = max(0.1, min(30.0, node_ci_s))  # mirror node's clamp
        ci_ms = int(node_ci_s * 1000)

        now = datetime.datetime.now()

        # Deduplicate burst packets: the nRF52 sends 3× ADV_CH_ALL per round,
        # all with the same seq. Packets within the same burst arrive within
        # ~5 ms of each other. If the same seq appears after >0.5 s the node
        # rebooted and seq restarted at the same value — checkpoint is a no-op
        # so every boot resets seq to 0. Treat that as a reboot, not a dup.
        if seq == self._last_seq:
            if (self._last_rx_time is None or
                    (now - self._last_rx_time).total_seconds() < 0.5):
                return
            # Same seq after long gap → reboot with seq reset to same value.
            if self._session_start is not None:
                elapsed = (now - self._session_start).total_seconds()
                ci_range = (
                    f"  ci {self._session_first_ci}→{self._prev_ci_ms} ms"
                    if self._session_first_ci is not None else ""
                )
                print(
                    f"  ** node rebooted  seq {self._last_seq}→{seq}"
                    f"  session #{self._session}: {self._round} rounds"
                    f" in {elapsed:.0f}s"
                    f"  loss={100*(1-self._rx/max(self._exp,1)):.1f}%"
                    f"{ci_range}"
                )
            self._reset_session()

        # ── First packet ever ────────────────────────────────────────────────
        if self._session_start is None:
            self._session_start = now
            resuming = f"  (joining mid-session, seq={seq})" if seq > 0 else ""
            rssi0 = changed.get("RSSI")
            rssi0_str = f"  rssi={int(rssi0)} dBm" if rssi0 is not None else ""
            print(f"[FOUND] Riotee node responding{rssi0_str}{resuming}")

        # ── Gap / reboot detection (seq wraps at 2^16) ───────────────────────
        if self._last_seq is not None:
            delta = (seq - self._last_seq) % 0x10000
            if delta > 0x8000:
                # Backward seq jump → node rebooted.
                elapsed = (now - self._session_start).total_seconds()
                ci_range = (
                    f"  ci {self._session_first_ci}→{self._prev_ci_ms} ms"
                    if self._session_first_ci is not None
                    else ""
                )
                print(
                    f"  ** node rebooted  seq {self._last_seq}→{seq}"
                    f"  session #{self._session}: {self._round} rounds"
                    f" in {elapsed:.0f}s  loss={100 * (1 - self._rx / max(self._exp, 1)):.1f}%"
                    f"{ci_range}"
                )
                self._reset_session()
                delta = seq + 1
            if delta > 1 and self._last_seq is not None:
                missed = delta - 1
                end = (self._last_seq + delta - 1) % 0x10000
                if self._real:
                    ci_hint = ""
                else:
                    missed_cis = []
                    for s in range(self._last_seq + 1, self._last_seq + 1 + min(missed, 5)):
                        missed_cis.append(
                            f"{self._ref.ci_for_seq_peek(s % 0x10000) * 1000:.0f}"
                        )
                    ci_hint = (
                        "  expected ci: "
                        + "/".join(missed_cis)
                        + (" ms" if len(missed_cis) == missed else f"… ms ({missed} total)")
                    )
                print(
                    f"  !! missed {missed} round(s): seq {self._last_seq + 1}..{end}{ci_hint}"
                )
            self._exp += delta
        else:
            self._exp += seq + 1
        self._rx += 1
        self._round += 1

        loss_pct = 100.0 * (1.0 - self._rx / self._exp)

        # ── Decode app payload ───────────────────────────────────────────────
        app_bytes = data[
            BONITO_ADV_HDR_LEN : BONITO_ADV_HDR_LEN + min(app_len, BONITO_ADV_APP_MAX)
        ]
        if app_len == 4:
            (counter,) = struct.unpack_from("<I", app_bytes)
            app_str = f"counter={counter}"
        else:
            app_str = f"app={app_bytes.hex()}"

        # ── CI verification ──────────────────────────────────────────────────
        # node_ci_s is derived from received mean+var (computed above).
        # In sim mode, compare against the reference model replaying the same
        # synthetic trace; in real mode there is no ground-truth trace.
        ref_ci_s = self._ref.reference_ci_for_seq(seq)  # advances ref always
        if self._real:
            match_str = "real"
        else:
            rel_err = abs(node_ci_s - ref_ci_s) / max(ref_ci_s, 1e-9)
            match_str = (
                "ok"
                if rel_err <= CI_REL_TOL
                else f"MISMATCH ref={ref_ci_s * 1000:.0f}ms"
            )

        # ── CI trend ─────────────────────────────────────────────────────────
        if self._prev_ci_ms is None:
            trend = " "
        elif ci_ms < self._prev_ci_ms - 1:
            trend = "↓"
        elif ci_ms > self._prev_ci_ms + 1:
            trend = "↑"
        else:
            trend = "→"
        if self._session_first_ci is None:
            self._session_first_ci = ci_ms

        # ── Next TX window ───────────────────────────────────────────────────
        next_tx = now + datetime.timedelta(milliseconds=ci_ms)
        next_tx_str = next_tx.strftime("%H:%M:%S")

        rssi = changed.get("RSSI")
        rssi_str = f"  rssi={int(rssi)} dBm" if rssi is not None else ""

        # ── Laptop peer update + joint CI ────────────────────────────────────
        self._laptop.update()
        joint_ci_ms = int(self._laptop.joint_ci(mean, node_sigma) * 1000)

        # ── Main line ────────────────────────────────────────────────────────
        print(
            f"seq={seq:5d}  ci={ci_ms:6d} ms {trend} [{match_str}]"
            f"  loss={loss_pct:4.1f}%  {app_str}{rssi_str}"
        )
        # ── Debug line ───────────────────────────────────────────────────────
        print(
            f"       session #{self._session} round {self._round:4d}"
            f"  next TX ~{next_tx_str} (+{ci_ms} ms)"
            f"  node µ={mean:.3f}s σ={node_sigma:.3f}s"
            f"  laptop µ={self._laptop.mean:.3f}s σ={self._laptop.sigma:.3f}s"
            f"  joint CI={joint_ci_ms} ms"
        )

        self._prev_ci_ms = ci_ms
        self._last_seq = seq
        self._last_rx_time = now


# ── Entry point ───────────────────────────────────────────────────────────────


def main() -> None:
    parser = argparse.ArgumentParser(description="Riotee Bonito BLE monitor")
    parser.add_argument(
        "--sim",
        action="store_true",
        help="Firmware was built with BONITO_SOURCE=sim (enables synthetic-trace CI verification)",
    )
    args = parser.parse_args()
    real = not args.sim

    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()

    set_scan_parameters()

    adapter = dbus.Interface(
        bus.get_object(BLUEZ_SERVICE, ADAPTER_PATH),
        "org.bluez.Adapter1",
    )
    adapter.SetDiscoveryFilter(
        {
            "Transport": dbus.String("le"),
            "DuplicateData": dbus.Boolean(True),
        }
    )

    scanner = BonitoScanner(real=real)
    bus.add_signal_receiver(
        scanner.on_properties_changed,
        dbus_interface="org.freedesktop.DBus.Properties",
        signal_name="PropertiesChanged",
        arg0="org.bluez.Device1",
        path_keyword="path",
    )

    adapter.StartDiscovery()
    print(
        f"Scanning for Riotee Bonito adverts"
        f" ({SCAN_INTERVAL_MS} ms interval, DuplicateData=True)..."
        f" Press Ctrl+C to stop."
    )

    loop = GLib.MainLoop()
    try:
        loop.run()
    except KeyboardInterrupt:
        print("\nScan stopped.")
        adapter.StopDiscovery()


if __name__ == "__main__":
    main()
