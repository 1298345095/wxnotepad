#!/usr/bin/python3
"""Observe real AT-SPI Peer.Ping replies while auto_insertion runs its 444 cases.

Run inside the repository's private display/D-Bus wrapper. The executable must
be one of the diagnostic copies with flushed case-loop-begin/complete markers.
The GUI retains its own 90-second limit; the caller allows separate monitor
startup/cleanup time. No accessibility desktop enumeration is used.
"""

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import resource
import signal
import subprocess
import threading
import time

import gi

gi.require_version('Gio', '2.0')
from gi.repository import Gio, GLib


CASE_COUNT = 444
PING_TIMEOUT_MS = 10_000
PING_INTERVAL_SECONDS = 1.0
BUS = 'org.freedesktop.DBus'
BUS_PATH = '/org/freedesktop/DBus'
BEGIN = re.compile(r'\[ATSPI-PROBE\] case-loop-begin total=(\d+)')
COMPLETE = re.compile(r'\[ATSPI-PROBE\] case-loop-complete total=(\d+)(?: max_case_ms=(\d+))?')
CASE = re.compile(r'case (\d+) language=(-?\d+) ch=(-?\d+)')
PASS = 'PASS: original character notifications, styles, read-only state and Undo/Redo'


def error_details(error):
    """Retain the distinction between a remote NoReply and a local Gio deadline."""
    remote = Gio.DBusError.get_remote_error(error)
    if remote == BUS + '.Error.NoReply' or error.matches(Gio.dbus_error_quark(), Gio.DBusError.NO_REPLY):
        kind = 'dbus_no_reply'
    elif error.matches(Gio.io_error_quark(), Gio.IOErrorEnum.TIMED_OUT):
        kind = 'gio_deadline_elapsed'
    elif error.matches(Gio.io_error_quark(), Gio.IOErrorEnum.CANCELLED):
        kind = 'cancelled'
    else:
        kind = 'other_error'
    return {'kind': kind, 'remote_error_name': remote, 'error_domain': error.domain,
            'error_code': error.code, 'error_message': error.message}


class PingProbe:
    def __init__(self, args):
        self.args = args
        self.started_ns = time.monotonic_ns()
        self.app_started_ns = None
        self.process = None
        self.connection = None
        self.loop = GLib.MainLoop()
        self.cancel = Gio.Cancellable()
        self.finished = threading.Event()
        self.deadline_expired = threading.Event()
        self.watchdog = None
        self.interrupted = None
        self.fatal_error = None
        self.name = None
        self.known_owners = {}
        self.next_lookup = 0.0
        self.next_ping = 0.0
        self.pending = None
        self.pings = []
        self.case_indices = []
        self.begin_ns = None
        self.complete_ns = None
        self.max_case_ms = None
        self.pass_seen = False
        self.fail_lines = []
        self.protocol_errors = []
        self.output_buffer = bytearray()
        self.output_eof = False
        self.report = args.report_dir.resolve()
        self.report.mkdir(parents=True, exist_ok=False)
        self.events = (self.report / 'events.jsonl').open('w', encoding='utf-8')
        self.output = (self.report / 'gui.log').open('wb')

    def emit(self, event, console=True, **fields):
        now = time.monotonic_ns()
        row = {'event': event, 'utc': datetime.now(timezone.utc).isoformat(timespec='milliseconds'),
               'mono_ns': now, 'monitor_elapsed_ms': round((now - self.started_ns) / 1_000_000, 3),
               'pid': self.process.pid if self.process else None, 'name': self.name, **fields}
        if self.app_started_ns is not None:
            row['gui_elapsed_ms'] = round((now - self.app_started_ns) / 1_000_000, 3)
        line = json.dumps(row, ensure_ascii=False)
        self.events.write(line + '\n')
        self.events.flush()
        if console:
            print(line, flush=True)

    def connect(self):
        # Cancellable also bounds the otherwise timeout-less bus connection
        # handshake. This is monitor startup, before the GUI's 90-second clock.
        timer = threading.Timer(10, self.cancel.cancel)
        timer.daemon = True
        timer.start()
        try:
            session = Gio.bus_get_sync(Gio.BusType.SESSION, self.cancel)
            session.set_exit_on_close(False)
            address = session.call_sync(
                'org.a11y.Bus', '/org/a11y/bus', 'org.a11y.Bus', 'GetAddress',
                None, GLib.VariantType.new('(s)'), Gio.DBusCallFlags.NONE,
                10_000, self.cancel).unpack()[0]
            self.connection = Gio.DBusConnection.new_for_address_sync(
                address, Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT |
                Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION, None, self.cancel)
            self.connection.set_exit_on_close(False)
            self.emit('a11y_connected', monitor_name=self.connection.get_unique_name())
        finally:
            timer.cancel()

    def kill_group(self, sig):
        if self.process is not None:
            try:
                os.killpg(self.process.pid, sig)
            except ProcessLookupError:
                pass

    def interrupt(self, signum, _frame):
        self.interrupted = signum
        self.cancel.cancel()
        self.kill_group(signal.SIGTERM)
        self.loop.quit()

    def enforce_deadline(self):
        elapsed = (time.monotonic_ns() - self.app_started_ns) / 1_000_000_000
        if not self.finished.wait(max(0.0, self.args.timeout - elapsed)):
            if self.process.poll() is not None:
                return
            self.deadline_expired.set()
            self.cancel.cancel()
            # Do not depend on GUI or GLib dispatch to enforce the GUI limit.
            self.kill_group(signal.SIGKILL)
            GLib.idle_add(self.stop_at_deadline)

    def stop_at_deadline(self):
        self.emit('gui_deadline', timeout_seconds=self.args.timeout)
        self.loop.quit()
        return GLib.SOURCE_REMOVE

    def launch(self):
        _, hard = resource.getrlimit(resource.RLIMIT_CORE)
        resource.setrlimit(resource.RLIMIT_CORE, (0, hard))
        self.app_started_ns = time.monotonic_ns()
        self.process = subprocess.Popen(
            [str(self.args.executable.resolve())], stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
        os.set_blocking(self.process.stdout.fileno(), False)
        self.emit('gui_started', executable=str(self.args.executable.resolve()),
                  cwd=str(Path.cwd()), backend=os.environ.get('GDK_BACKEND'),
                  timeout_seconds=self.args.timeout, core_soft_limit=0)
        self.watchdog = threading.Thread(target=self.enforce_deadline, daemon=True)
        self.watchdog.start()

    def line(self, raw):
        text = raw.decode('utf-8', errors='replace').rstrip('\r')
        self.emit('gui_output', console=False, text=text)
        if match := BEGIN.fullmatch(text):
            if self.begin_ns is not None or int(match[1]) != CASE_COUNT:
                self.protocol_errors.append('invalid or repeated begin marker')
            self.begin_ns = time.monotonic_ns()
            self.emit('case_loop_begin', total=int(match[1]))
        elif match := COMPLETE.fullmatch(text):
            if self.begin_ns is None or self.complete_ns is not None or int(match[1]) != CASE_COUNT:
                self.protocol_errors.append('invalid or repeated complete marker')
            if match[2] is None:
                self.protocol_errors.append('complete marker must include max_case_ms')
            self.complete_ns = time.monotonic_ns()
            self.max_case_ms = int(match[2]) if match[2] is not None else None
            self.emit('case_loop_complete', total=int(match[1]), max_case_ms=self.max_case_ms)
        elif match := CASE.fullmatch(text):
            self.case_indices.append(int(match[1]))
        elif text == PASS:
            self.pass_seen = True
            self.emit('gui_pass', text=text)
        elif text.startswith('FAIL:'):
            self.fail_lines.append(text)
            self.emit('gui_failure', text=text)

    def read_output(self):
        if self.process is None or self.output_eof:
            return
        # A bounded read prevents an unexpectedly noisy child from monopolizing
        # the observer. Normal complete fixture output is much smaller than this.
        remaining = 1_048_576
        while remaining:
            try:
                data = os.read(self.process.stdout.fileno(), min(65_536, remaining))
            except BlockingIOError:
                break
            if not data:
                self.output_eof = True
                if self.output_buffer:
                    self.line(bytes(self.output_buffer))
                    self.output_buffer.clear()
                break
            remaining -= len(data)
            self.output.write(data)
            self.output.flush()
            self.output_buffer.extend(data)
            while (end := self.output_buffer.find(b'\n')) >= 0:
                self.line(bytes(self.output_buffer[:end]))
                del self.output_buffer[:end + 1]

    def bus_call(self, method, parameters, reply_type):
        return self.connection.call_sync(
            BUS, BUS_PATH, BUS, method, parameters, GLib.VariantType.new(reply_type),
            Gio.DBusCallFlags.NONE, 500, self.cancel).unpack()

    def owner_pid(self, name):
        return self.bus_call('GetConnectionUnixProcessID', GLib.Variant('(s)', (name,)), '(u)')[0]

    def find_name(self):
        try:
            names = self.bus_call('ListNames', None, '(as)')[0]
        except GLib.Error as error:
            self.emit('name_lookup_error', **error_details(error))
            return
        matches = []
        for name in names:
            if not name.startswith(':') or name == self.connection.get_unique_name():
                continue
            if name not in self.known_owners:
                try:
                    self.known_owners[name] = self.owner_pid(name)
                except GLib.Error as error:
                    self.emit('owner_lookup_error', lookup_name=name, **error_details(error))
                    continue
            if self.known_owners[name] == self.process.pid:
                matches.append(name)
        if len(matches) > 1:
            raise RuntimeError(f'GUI PID has multiple a11y connections; target is ambiguous: {matches}')
        if matches:
            self.name = matches[0]
            self.emit('gui_bus_bound', owner_pid=self.known_owners[self.name])

    def active(self):
        return (self.begin_ns is not None and self.complete_ns is None and
                self.process.poll() is None)

    def send_ping(self):
        request = {'sequence': len(self.pings) + 1, 'name': self.name,
                   'pid': self.process.pid, 'sent_ns': time.monotonic_ns(),
                   'sent_during_case_loop': self.active(), 'cancel': Gio.Cancellable(),
                   'finished': False}
        self.pending = request
        self.emit('ping_send', sequence=request['sequence'], sent_ns=request['sent_ns'],
                  timeout_ms=PING_TIMEOUT_MS, case_lines_seen=len(self.case_indices))
        try:
            self.connection.call(
                self.name, '/', 'org.freedesktop.DBus.Peer', 'Ping', None,
                GLib.VariantType.new('()'), Gio.DBusCallFlags.NONE, PING_TIMEOUT_MS,
                request['cancel'], self.ping_finished, request)
        except GLib.Error as error:
            self.record_reply(request, error_details(error))

    def ping_finished(self, connection, result, request):
        try:
            connection.call_finish(result)
            details = {'kind': 'reply', 'remote_error_name': None}
        except GLib.Error as error:
            details = error_details(error)
        if not request['finished']:
            try:
                self.record_reply(request, details)
            except Exception as error:
                self.fatal_error = repr(error)
                self.loop.quit()

    def record_reply(self, request, details):
        finished_ns = time.monotonic_ns()
        latency_ms = (finished_ns - request['sent_ns']) / 1_000_000
        # Read flushed completion markers before classifying an error; shutdown
        # after 444 successes must not become an apparent mid-loop NoReply.
        self.read_output()
        live_owner = None
        owner_error = None
        if self.active() and details['kind'] in ('dbus_no_reply', 'gio_deadline_elapsed'):
            try:
                live_owner = self.owner_pid(request['name']) == self.process.pid
            except GLib.Error as error:
                live_owner = False
                owner_error = error_details(error)
            self.read_output()
        mid_loop = request['sent_during_case_loop'] and self.active()
        deadline_elapsed = latency_ms >= PING_TIMEOUT_MS
        missed_reply = (details['kind'] == 'dbus_no_reply' or
                        (details['kind'] == 'gio_deadline_elapsed' and deadline_elapsed))
        row = {key: value for key, value in request.items() if key not in ('cancel', 'finished')}
        row.update(details, finished_ns=finished_ns, latency_ms=round(latency_ms, 3),
                   mid_case_loop=mid_loop, deadline_elapsed=deadline_elapsed,
                   owner_still_matches=live_owner, owner_check_error=owner_error,
                   qualifying_timeout=mid_loop and missed_reply and live_owner is True)
        self.pings.append(row)
        request['finished'] = True
        self.pending = None
        self.next_ping = time.monotonic() + PING_INTERVAL_SECONDS
        self.emit('ping_result', **row)

    def tick(self):
        try:
            self.read_output()
            if self.process.poll() is not None:
                self.emit('gui_exited', exit_code=self.process.returncode)
                self.loop.quit()
                return GLib.SOURCE_REMOVE
            if self.deadline_expired.is_set() or self.interrupted is not None:
                self.loop.quit()
                return GLib.SOURCE_REMOVE
            now = time.monotonic()
            if self.name is None and now >= self.next_lookup:
                self.find_name()
                self.next_lookup = time.monotonic() + 0.2
                self.read_output()
            if self.name and self.active() and self.pending is None and now >= self.next_ping:
                self.send_ping()
            return GLib.SOURCE_CONTINUE
        except Exception as error:
            self.fatal_error = repr(error)
            self.emit('monitor_error', error=self.fatal_error)
            self.loop.quit()
            return GLib.SOURCE_REMOVE

    def cleanup(self):
        self.finished.set()
        self.cancel.cancel()
        if self.pending is not None:
            request = self.pending
            request['finished'] = True
            request['cancel'].cancel()
            row = {key: value for key, value in request.items() if key not in ('cancel', 'finished')}
            row.update(kind='cancelled_during_cleanup', mid_case_loop=False,
                       qualifying_timeout=False,
                       latency_ms=round((time.monotonic_ns() - request['sent_ns']) / 1_000_000, 3),
                       reason='case_loop_complete' if self.complete_ns is not None else 'monitor_stopped')
            self.pings.append(row)
            self.emit('ping_cancelled', **row)
            self.pending = None
        if self.process is not None:
            if self.process.poll() is None:
                self.kill_group(signal.SIGTERM)
                try:
                    self.process.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    self.kill_group(signal.SIGKILL)
                    self.process.wait(timeout=1)
            # Reap any still-owned descendants even if their GUI leader exited.
            self.kill_group(signal.SIGKILL)
            self.read_output()
            self.process.stdout.close()
        if self.watchdog is not None:
            self.watchdog.join(timeout=1)
        if self.connection is not None:
            close_cancel = Gio.Cancellable()
            timer = threading.Timer(1, close_cancel.cancel)
            timer.daemon = True
            timer.start()
            try:
                self.connection.close_sync(close_cancel)
            except GLib.Error as error:
                self.emit('monitor_connection_close', **error_details(error))
            finally:
                timer.cancel()

    def summary(self):
        exit_code = self.process.returncode if self.process else None
        complete = (exit_code == 0 and self.begin_ns is not None and self.complete_ns is not None and
                    self.case_indices == list(range(CASE_COUNT)) and self.pass_seen and
                    not self.fail_lines and not self.protocol_errors)
        middle = [row for row in self.pings if row['mid_case_loop']]
        replies = [row for row in middle if row['kind'] == 'reply']
        missed = [row for row in middle if row['qualifying_timeout']]
        other = [row for row in middle if row['kind'] != 'reply' and not row['qualifying_timeout']]
        reasons = []
        if not complete:
            reasons.append('GUI must exit 0 with both markers, cases 0..443, and the unchanged PASS output')
        if self.name is None:
            reasons.append('No unique a11y name was bound to the real GUI PID')
        if self.deadline_expired.is_set():
            reasons.append('GUI exceeded its original application timeout')
        if self.interrupted is not None:
            reasons.append(f'Monitor interrupted by signal {self.interrupted}')
        if self.fatal_error:
            reasons.append(self.fatal_error)
        if self.args.expect == 'timeout':
            if not missed:
                reasons.append('No confirmed mid-loop NoReply or 10-second Gio deadline with a live PID binding')
            if other:
                reasons.append('Control also reported a different mid-loop error')
        else:
            if len(replies) < 2:
                reasons.append('At least two completed mid-loop Ping replies are required')
            if missed or other:
                reasons.append('Responsive variant reported a mid-loop Ping error')
        return {'success': not reasons, 'expect': self.args.expect, 'reasons': reasons,
                'pid': self.process.pid if self.process else None, 'name': self.name,
                'executable': str(self.args.executable.resolve()), 'cwd': str(Path.cwd()),
                'backend': os.environ.get('GDK_BACKEND'), 'gui_exit_code': exit_code,
                'application_timeout_seconds': self.args.timeout,
                'application_deadline_expired': self.deadline_expired.is_set(),
                'all_444_cases_passed': complete, 'case_lines': len(self.case_indices),
                'begin_observed_ns': self.begin_ns, 'complete_observed_ns': self.complete_ns,
                'max_case_ms': self.max_case_ms, 'mid_loop_replies': len(replies),
                'mid_loop_timeouts': len(missed), 'mid_loop_other_errors': len(other),
                'ping_timeout_ms': PING_TIMEOUT_MS, 'pings': self.pings,
                'fail_lines': self.fail_lines, 'protocol_errors': self.protocol_errors,
                'monitor_seconds': round((time.monotonic_ns() - self.started_ns) / 1_000_000_000, 3)}

    def run(self):
        previous_handlers = {}
        for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGQUIT):
            previous_handlers[sig] = signal.signal(sig, self.interrupt)
        try:
            if not os.environ.get('DBUS_SESSION_BUS_ADDRESS') or not os.environ.get('WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS'):
                raise RuntimeError('Run this observer inside the private D-Bus/display fixture')
            if os.environ.get('GDK_BACKEND') not in ('x11', 'wayland'):
                raise RuntimeError('The private fixture must select x11 or wayland explicitly')
            self.connect()
            if self.cancel.is_cancelled():
                raise RuntimeError('Monitor startup was cancelled or exceeded its 10-second budget')
            self.launch()
            GLib.timeout_add(50, self.tick)
            if self.tick() == GLib.SOURCE_CONTINUE:
                self.loop.run()
        except Exception as error:
            self.fatal_error = repr(error)
            self.emit('monitor_error', error=self.fatal_error)
        finally:
            try:
                self.cleanup()
            except Exception as error:
                self.fatal_error = f'Cleanup failed: {error!r}'
                self.emit('cleanup_error', error=self.fatal_error)
            for sig, handler in previous_handlers.items():
                signal.signal(sig, handler)
        summary = self.summary()
        (self.report / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
        self.emit('summary', **summary)
        self.output.close()
        self.events.close()
        return 0 if summary['success'] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--expect', choices=('timeout', 'responsive'), required=True)
    parser.add_argument('--report-dir', type=Path, required=True)
    parser.add_argument('--timeout', type=float, default=90)
    args = parser.parse_args()
    if not 0 < args.timeout <= 90:
        parser.error('--timeout must be positive and cannot exceed the original 90-second limit')
    if not args.executable.is_file() or not os.access(args.executable, os.X_OK):
        parser.error('--executable must name an existing executable file')
    return PingProbe(args).run()


if __name__ == '__main__':
    raise SystemExit(main())
