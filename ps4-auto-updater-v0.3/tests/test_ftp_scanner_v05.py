"""Read-only integration tests for the PS4 v0.5 FTP scanner.

Simulates GoldHEN FTP on localhost. No console access, real FTP
server or internet needed. All SFO metadata is synthetic.
"""
import json
import os
from pathlib import Path
import shutil
import socket
import socketserver
import struct
import subprocess
import tempfile
import threading
import unittest

SCANNER = Path(__file__).resolve().parents[1] / "ps4" / "src" / "ftp_scanner_v05.c"
READ_ONLY_COMMANDS = {"USER", "PASS", "TYPE", "PASV", "LIST", "RETR", "QUIT"}


def sfo(title_id, name="Test Game", version="01.04", category="gd",
        include_title_id=True, content_id=None):
    fields = [
        ("TITLE", name),
        ("APP_VER", version),
        ("CONTENT_ID", content_id if content_id is not None else
         "IV0000-" + title_id + "_00-SCANNER000000000"),
        ("CATEGORY", category),
    ]
    if include_title_id:
        fields.insert(0, ("TITLE_ID", title_id))
    keys, values, entries = b"", b"", b""
    for key, val in fields:
        key_offset = len(keys)
        value_offset = len(values)
        keys += key.encode("ascii") + b"\0"
        encoded = val.encode("utf8") + b"\0"
        values += encoded
        entries += struct.pack("<HHIII", key_offset, 0x0204,
                               len(encoded), len(encoded), value_offset)
    key_start = 20 + len(entries)
    value_start = key_start + len(keys)
    header = struct.pack("<IIIII", 0x46535000, 0x101,
                         key_start, value_start, len(fields))
    return header + entries + keys + values


def sfo_corruption_repro(title_id="CUSA02343"):
    """23-entry valid PSF with exactly one LF in TITLE_ID length at entry 20.

    Mirrors the user-observed 23-entry/entry-20 0x0a0d symptom.
    Its byte layout is synthetic: not a console-file fixture.
    """
    fields = [("K%02d" % i, b"TEST", 0x0404) for i in range(20)]
    fields.extend([
        ("TITLE_ID", title_id.encode("ascii") + b"\0", 0x0204),
        ("CATEGORY", b"gd\0", 0x0204),
        ("APP_VER", b"01.07\0", 0x0204),
    ])
    keys = bytearray()
    vals = bytearray()
    index = bytearray()
    for key, raw, fmt in fields:
        kpos = len(keys)
        vpos = len(vals)
        keys.extend(key.encode("ascii") + b"\0")
        vals.extend(raw)
        index.extend(struct.pack("<HHIII", kpos, fmt, len(raw), len(raw), vpos))
    header = struct.pack("<IIIII", 0x46535000, 0x101,
                         20 + len(index), 20 + len(index) + len(keys),
                         len(fields))
    original = header + index + keys + vals
    assert len(fields) == 23
    assert original.count(b"\n") == 1, original.count(b"\n")
    return original


class MockHandler(socketserver.StreamRequestHandler):
    def respond(self, text):
        # Original FTPS4 often terminates control replies with LF only.
        ending = "\n" if self.server.lf_only_replies else "\r\n"
        self.wfile.write((text + ending).encode("ascii"))
        self.wfile.flush()

    def handle(self):
        self.request.settimeout(5)
        data_socket = None
        logged_in = not self.server.require_login
        self.respond("220 mock FTP ready")
        try:
            while True:
                if self.server.packet_strict:
                    # FTPS4-style control loop: each recv is treated as
                    # an entire command (no partial-line buffering).
                    raw = self.request.recv(4096)
                    if raw and (not raw.endswith(b"\r\n") or raw.count(b"\n") != 1):
                        self.server.commands.append("FRAGMENT")
                        self.respond("500 Command received without CRLF")
                        break
                else:
                    raw = self.rfile.readline()
                if not raw:
                    break
                command, _, argument = raw.decode("ascii", "replace").strip().partition(" ")
                command = command.upper()
                self.server.commands.append(command)
                if command == "USER":
                    if self.server.deny_login:
                        self.respond("530 rejected")
                    elif self.server.challenge_password:
                        self.respond("331 password needed")
                    else:
                        logged_in = True
                        self.respond("230 logged in")
                elif command == "PASS":
                    if self.server.deny_login:
                        self.respond("530 rejected")
                    else:
                        logged_in = True
                        self.respond("230 OK")
                elif command == "TYPE":
                    if self.server.type_unknown:
                        self.respond("500 Unknown command TYPE")
                    else:
                        self.respond("200 binary" if logged_in else "530 Not logged in")
                elif command == "PASV":
                    if self.server.require_login and not logged_in:
                        self.respond("530 Not logged in")
                        continue
                    data_socket = socket.socket()
                    data_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                    data_socket.bind(("127.0.0.1", 0))
                    data_socket.listen(1)
                    data_socket.settimeout(5)
                    port = data_socket.getsockname()[1]
                    self.respond("227 Entering Passive Mode (%s,%d,%d)" %
                                 (self.server.pasv_advertisement, port // 256, port % 256))
                elif command in ("LIST", "RETR"):
                    payload = None
                    if command == "LIST":
                        if argument == "/system_data/priv/appmeta":
                            payload = self.server.listing
                        elif argument == "/system_data/priv/appmeta/external":
                            payload = self.server.external_listing
                    else:
                        prefix = "/system_data/priv/appmeta/"
                        suffix = "/param.sfo"
                        if argument.startswith(prefix) and argument.endswith(suffix):
                            title_id = argument[len(prefix):-len(suffix)]
                            payload = self.server.metadata.get(title_id)
                    if payload is None:
                        self.respond("550 missing")
                    else:
                        self.respond("150 transfer starting")
                        conn, _ = data_socket.accept()
                        with conn:
                            try:
                                conn.sendall(payload)
                            except OSError:
                                pass
                        self.respond("226 transfer complete")
                    if data_socket:
                        data_socket.close()
                        data_socket = None
                elif command == "QUIT":
                    self.respond("221 goodbye")
                    break
                else:
                    self.respond("502 unsupported")
        except (OSError, TimeoutError):
            pass
        finally:
            if data_socket is not None:
                data_socket.close()


class MockFTP(socketserver.TCPServer):
    allow_reuse_address = True
    def __init__(self, listing, metadata):
        super().__init__(("127.0.0.1", 0), MockHandler)
        self.listing = listing
        self.metadata = metadata
        self.external_listing = b""
        self.commands = []
        self.pasv_advertisement = "127,0,0,1"
        self.deny_login = False
        self.require_login = False
        self.challenge_password = False
        self.type_unknown = False
        self.packet_strict = False
        self.lf_only_replies = False


class ScannerFTPTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which("cc"):
            raise unittest.SkipTest("C compiler unavailable")
        cls.temp = tempfile.TemporaryDirectory()
        cls.executable = str(Path(cls.temp.name) / "ftp-scanner")
        cls.limited_executable = str(Path(cls.temp.name) / "ftp-scanner-cap2")
        for binary, extra_flags in (
            (cls.executable, []),
            (cls.limited_executable, ["-DMAX_GAMES=2"]),
        ):
            subprocess.run(
                ["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2",
                 *extra_flags, "-o", binary, str(SCANNER)],
                check=True, capture_output=True, text=True,
            )

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "temp"):
            cls.temp.cleanup()

    def run_client(self, port, executable=None):
        with tempfile.TemporaryDirectory() as directory:
            inv = Path(directory) / "inventory.json"
            diagnostic = Path(directory) / "diagnostic.txt"
            env = dict(os.environ,
                       PS4_FTP_TEST_PORT=str(port),
                       PS4_FTP_TEST_OUT=str(inv),
                       PS4_FTP_TEST_REPORT=str(diagnostic))
            proc = subprocess.run(
                [executable or self.executable], env=env, text=True,
                capture_output=True, timeout=20,
            )
            self.assertEqual(proc.returncode, 0, proc.stderr)
            return json.loads(inv.read_text()), diagnostic.read_text()

    def with_server(self, listing, metadata, *, passthrough=None,
                    executable=None):
        with MockFTP(listing, metadata) as server:
            if passthrough:
                passthrough(server)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                inventory, diagnostic = self.run_client(
                    server.server_address[1], executable=executable)
                return inventory, diagnostic, list(server.commands)
            finally:
                server.shutdown()
                thread.join(timeout=4)

    def test_two_games_ignore_dlc_and_duplicates(self):
        listing = (
            b"drwxr-xr-x 1 root root 0 Nov 08 12:00 CUSA02343\r\n"
            b"drwxr-xr-x 1 root root 0 Nov 08 12:00 CUSA02343\r\n"
            b"drwxr-xr-x 1 root root 0 Nov 08 12:00 CUSA00411\r\n"
            b"drwxr-xr-x 1 root root 0 Nov 08 12:00 CUSA99999\r\n"
            b"drwxr-xr-x 1 root root 0 Nov 08 12:00 WRONGID\r\n"
        )
        inventory, diagnostic, commands = self.with_server(
            listing,
            {"CUSA02343": sfo("CUSA02343", "Uncharted Test", "01.07"),
             "CUSA00411": sfo("CUSA00411", "Other Game", "02.04"),
             "CUSA99999": sfo("CUSA99999", "DLC", "01.00", "ac")},
        )
        self.assertEqual(inventory["schema"], 5)
        self.assertEqual(
            [c["cusa"] for c in inventory["directory_candidates"]],
            ["CUSA02343", "CUSA00411", "CUSA99999"]
        )
        self.assertEqual(
            [c["metadata_verified"] for c in inventory["directory_candidates"]],
            [True, True, False]
        )
        self.assertTrue(inventory["scan_complete"])
        self.assertEqual([g["cusa"] for g in inventory["games"]],
                         ["CUSA02343", "CUSA00411"])
        self.assertEqual(inventory["games"][0]["app_ver"], "01.07")
        self.assertEqual(inventory["games"][0]["install_state"], "UNKNOWN")
        self.assertFalse(inventory["download_enabled"])
        self.assertFalse(inventory["install_enabled"])
        self.assertIn("Metadata records: 2", diagnostic)
        self.assertIn("FTP_GREETING_ACCEPTED: 1", diagnostic)
        self.assertIn("FTP_GREETING_CODE: 220", diagnostic)
        self.assertIn("FTP_COMMAND_MODE (0=PASV-first, no TYPE): 0", diagnostic)
        self.assertEqual(commands[0], "PASV")
        self.assertNotIn("TYPE", commands)
        self.assertNotIn("USER", commands)
        self.assertNotIn("PASS", commands)
        self.assertTrue(set(commands).issubset(READ_ONLY_COMMANDS), commands)
        self.assertEqual(commands.count("RETR"), 3)

    def test_ascii_mode_sfo_newlines_are_restored_safely(self):
        listing = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
        original = sfo("CUSA02343", "Recovered Game", "01.07")
        damaged = original.replace(b"\n", b"\r\n")
        self.assertGreater(len(damaged), len(original))
        inventory, diagnostic, commands = self.with_server(
            listing, {"CUSA02343": damaged})
        self.assertEqual(len(inventory["games"]), 1)
        self.assertEqual(inventory["games"][0]["name"], "Recovered Game")
        self.assertTrue(inventory["scan_complete"])
        self.assertIn("SFO ASCII_RECOVERED CUSA02343", diagnostic)
        self.assertNotIn("TYPE", commands)

    def test_exact_entry_twenty_ascii_expansion_safely_recovers(self):
        listing = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
        original = sfo_corruption_repro()
        expanded = original.replace(b"\n", b"\r\n")
        self.assertEqual(len(expanded), len(original) + 1)
        # Entry 20's original string length 10 is converted to 0x0a0d.
        self.assertEqual(struct.unpack_from("<I", expanded, 20 + 20*16 + 4)[0],
                         0x0a0d)
        inventory, diagnostic, _ = self.with_server(
            listing, {"CUSA02343": expanded})
        self.assertTrue(inventory["scan_complete"])
        self.assertEqual(inventory["games"][0]["cusa"], "CUSA02343")
        self.assertEqual(inventory["games"][0]["app_ver"], "01.07")
        self.assertIn("SFO ASCII_RECOVERED CUSA02343", diagnostic)

    def test_ascii_recovery_cannot_accept_wrong_content_id(self):
        listing = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
        damaged = sfo("CUSA02343", content_id=(
            "IV0000-CUSA99999_00-SCANNER000000000")).replace(b"\n", b"\r\n")
        inventory, diagnostic, _ = self.with_server(
            listing, {"CUSA02343": damaged})
        self.assertEqual(inventory["games"], [])
        self.assertFalse(inventory["scan_complete"])
        self.assertNotIn("SFO ASCII_RECOVERED", diagnostic)

    def test_missing_category_is_not_a_confirmed_game(self):
        listing = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
        inventory, diagnostic, _ = self.with_server(
            listing, {"CUSA02343": sfo("CUSA02343", category="")})
        self.assertEqual(inventory["games"], [])
        self.assertFalse(inventory["scan_complete"])
        self.assertIn("SFO CUSA02343: -37", diagnostic)

    def test_corrupt_sfo_is_not_counted(self):
        listing = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
        inventory, diagnostic, commands = self.with_server(
            listing, {"CUSA02343": b"not an SFO"})
        self.assertEqual(inventory["games"], [])
        self.assertFalse(inventory["scan_complete"])
        self.assertIn("SFO FILE CUSA02343 bytes=10", diagnostic)
        self.assertIn("SFO CUSA02343: -31", diagnostic)
        self.assertTrue(set(commands).issubset(READ_ONLY_COMMANDS), commands)

    def test_ftp_unavailable_is_reported_without_inventing_games(self):
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        inventory, diagnostic = self.run_client(port)
        self.assertEqual(inventory["games"], [])
        self.assertFalse(inventory["scan_complete"])
        self.assertIn("CONTROL_CONNECT_FAILED", diagnostic)

    def test_oversized_listing_cannot_create_partial_game(self):
        listing = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n" * 3500
        inventory, diagnostic, commands = self.with_server(
            listing, {"CUSA02343": sfo("CUSA02343")})
        self.assertEqual(inventory["games"], [])
        self.assertFalse(inventory["scan_complete"])
        self.assertIn("LIST internal: -16", diagnostic)
        self.assertNotIn("Metadata records: 1", diagnostic)
        self.assertTrue(set(commands).issubset(READ_ONLY_COMMANDS), commands)


    def test_external_listing_unavailable_is_explicitly_incomplete(self):
        inventory, diagnostic, _ = self.with_server(
            b"", {}, passthrough=lambda server: setattr(
                server, "external_listing", None))
        self.assertFalse(inventory["scan_complete"])
        self.assertIn("LIST external: -13", diagnostic)
        self.assertIn("External listing available: 0", diagnostic)
        self.assertIn("Primary listing available: 1", diagnostic)

    def test_empty_listing_is_valid_and_complete(self):
        inventory, diagnostic, commands = self.with_server(b"", {})
        self.assertEqual(inventory["games"], [])
        self.assertTrue(inventory["scan_complete"])
        self.assertIn("Scan complete: 1", diagnostic)
        self.assertTrue(set(commands).issubset(READ_ONLY_COMMANDS), commands)

    def test_pasv_server_cannot_redirect_to_another_host(self):
        listing = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
        inventory, diagnostic, commands = self.with_server(
            listing, {"CUSA02343": sfo("CUSA02343")},
            passthrough=lambda server: setattr(server, "pasv_advertisement",
                                               "10,20,30,40"))
        self.assertEqual(len(inventory["games"]), 1)
        self.assertTrue(inventory["scan_complete"])
        self.assertTrue(set(commands).issubset(READ_ONLY_COMMANDS), commands)

    def test_mismatched_title_id_is_never_in_inventory(self):
        listing = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
        inventory, diagnostic, _ = self.with_server(
            listing, {"CUSA02343": sfo("CUSA00001")})
        self.assertEqual(inventory["games"], [])
        self.assertFalse(inventory["scan_complete"])
        self.assertIn("SFO CUSA02343: -34", diagnostic)

    def test_ftp_login_rejection_is_explicitly_incomplete(self):
        def configure(server):
            server.require_login = True
            server.deny_login = True

        inventory, diagnostic, commands = self.with_server(
            b"", {}, passthrough=configure)
        self.assertEqual(inventory["games"], [])
        self.assertFalse(inventory["scan_complete"])
        self.assertIn("FTP_PASV_CODE: 530", diagnostic)
        self.assertIn("FTP_USER_CODE: 530", diagnostic)
        self.assertIn("FTP_GREETING_ACCEPTED: 1", diagnostic)
        self.assertIn("USER", commands)
        self.assertNotIn("PASS", commands)
        self.assertTrue(set(commands).issubset(READ_ONLY_COMMANDS), commands)

    def test_legacy_server_can_request_anonymous_login(self):
        inventory, diagnostic, commands = self.with_server(
            b"", {},
            passthrough=lambda server: setattr(server, "require_login", True))
        self.assertEqual(inventory["games"], [])
        self.assertTrue(inventory["scan_complete"])
        self.assertIn("FTP_PASV_CODE: 530", diagnostic)
        self.assertIn("FTP_USER_CODE: 230", diagnostic)
        self.assertIn("FTP_AUTH_MODE (0=no login,1=anonymous): 1", diagnostic)
        self.assertEqual(commands[:3], ["PASV", "USER", "PASV"])
        self.assertNotIn("PASS", commands)

    def test_legacy_server_password_challenge_is_supported(self):
        def configure(server):
            server.require_login = True
            server.challenge_password = True

        inventory, diagnostic, commands = self.with_server(
            b"", {}, passthrough=configure)
        self.assertTrue(inventory["scan_complete"])
        self.assertIn("FTP_USER_CODE: 331", diagnostic)
        self.assertIn("FTP_PASS_CODE: 230", diagnostic)
        self.assertEqual(commands[:4], ["PASV", "USER", "PASS", "PASV"])

    def test_real_console_type_500_still_lists_games(self):
        listing = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
        inventory, diagnostic, commands = self.with_server(
            listing,
            {"CUSA02343": sfo("CUSA02343", "Uncharted Test", "01.04")},
            passthrough=lambda server: setattr(server, "type_unknown", True),
        )
        self.assertEqual([g["cusa"] for g in inventory["games"]], ["CUSA02343"])
        self.assertTrue(inventory["scan_complete"])
        self.assertIn("FTP_PASV_CODE: 227", diagnostic)
        self.assertNotIn("TYPE", commands)
        self.assertNotIn("USER", commands)
        self.assertTrue(set(commands).issubset(READ_ONLY_COMMANDS), commands)

    def test_type_500_then_pasv_530_requires_anonymous(self):
        def configure(server):
            server.type_unknown = True
            server.require_login = True

        inventory, diagnostic, commands = self.with_server(
            b"", {}, passthrough=configure
        )
        self.assertTrue(inventory["scan_complete"])
        self.assertIn("FTP_PASV_CODE: 530", diagnostic)
        self.assertIn("FTP_USER_CODE: 230", diagnostic)
        self.assertIn("FTP_PASV_AFTER_AUTH_CODE: 227", diagnostic)
        self.assertEqual(commands[:3], ["PASV", "USER", "PASV"])
        self.assertNotIn("TYPE", commands)
        self.assertNotIn("PASS", commands)
        self.assertTrue(set(commands).issubset(READ_ONLY_COMMANDS), commands)


    def test_ftps4_recv_packet_parser_needs_single_write_per_command(self):
        listing = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
        inventory, diagnostic, commands = self.with_server(
            listing, {"CUSA02343": sfo("CUSA02343", "Packet Sensitive Game")},
            passthrough=lambda server: setattr(server, "packet_strict", True),
        )
        self.assertEqual(len(inventory["games"]), 1)
        self.assertTrue(inventory["scan_complete"])
        self.assertEqual(commands[0], "PASV")
        self.assertNotIn("FRAGMENT", commands)
        self.assertNotIn("TYPE", commands)
        self.assertTrue(set(commands).issubset(READ_ONLY_COMMANDS), commands)

    def test_ftps4_style_server_rejects_partial_ftp_command(self):
        with MockFTP(b"", {}) as server:
            server.packet_strict = True
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                with socket.create_connection(server.server_address, timeout=4) as conn:
                    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                    file = conn.makefile("rb")
                    self.assertTrue(file.readline().startswith(b"220"))
                    conn.sendall(b"PASV")
                    self.assertTrue(file.readline().startswith(b"500"))
                self.assertIn("FRAGMENT", server.commands)
            finally:
                server.shutdown()
                thread.join(timeout=4)


    def test_real_ftps4_per_recv_commands_and_lf_only_replies(self):
        def configure(server):
            server.packet_strict = True
            server.lf_only_replies = True

        listing = b"drwxr-xr-x 1 ps4 ps4 0 Nov 08 12:00 CUSA02343\r\n"
        inventory, diagnostic, commands = self.with_server(
            listing,
            {"CUSA02343": sfo("CUSA02343", "LF-only FTPS4 Test")},
            passthrough=configure,
        )
        self.assertEqual(len(inventory["games"]), 1)
        self.assertEqual(inventory["games"][0]["cusa"], "CUSA02343")
        self.assertTrue(inventory["scan_complete"])
        self.assertIn("FTP_GREETING_CODE: 220", diagnostic)
        self.assertIn("FTP_PASV_CODE: 227", diagnostic)
        self.assertNotIn("FRAGMENT", commands)
        self.assertNotIn("TYPE", commands)
        self.assertTrue(set(commands).issubset(READ_ONLY_COMMANDS), commands)


    def test_missing_title_id_can_be_verified_from_content_id(self):
        listing = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
        inventory, diagnostic, commands = self.with_server(
            listing, {"CUSA02343": sfo("CUSA02343", include_title_id=False)})
        self.assertEqual([g["cusa"] for g in inventory["games"]],
                         ["CUSA02343"])
        self.assertTrue(inventory["scan_complete"])
        self.assertIn("SFO ID from CONTENT_ID: CUSA02343", diagnostic)
        self.assertTrue(set(commands).issubset(READ_ONLY_COMMANDS), commands)

    def test_content_id_conflict_cannot_establish_a_game(self):
        listing = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
        inventory, diagnostic, _ = self.with_server(
            listing, {"CUSA02343": sfo("CUSA02343",
                                       content_id="IV0000-CUSA99999_00-SCANNER000000000")})
        self.assertEqual(inventory["games"], [])
        self.assertFalse(inventory["scan_complete"])
        self.assertIn("SFO CUSA02343: -35", diagnostic)

    def test_unverifiable_directory_is_candidate_not_installed_game(self):
        listing = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
        inventory, diagnostic, _ = self.with_server(
            listing, {"CUSA02343": b""})
        self.assertEqual(inventory["games"], [])
        self.assertFalse(inventory["scan_complete"])
        self.assertIn("Directory candidates: 1", diagnostic)
        self.assertIn("SFO FILE CUSA02343 bytes=0", diagnostic)
        self.assertIn("SFO CUSA02343: -30", diagnostic)
        self.assertEqual(
            inventory["directory_candidates"][0]["cusa"], "CUSA02343")
        self.assertFalse(
            inventory["directory_candidates"][0]["metadata_verified"])

    def test_patch_metadata_is_not_a_base_game(self):
        listing = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
        inventory, diagnostic, _ = self.with_server(
            listing, {"CUSA02343": sfo("CUSA02343", category="gp")})
        self.assertEqual(inventory["games"], [])
        self.assertTrue(inventory["scan_complete"])
        self.assertIn("SFO FIELDS CUSA02343", diagnostic)
        self.assertIn("SFO invalid: 0", diagnostic)

    def test_malformed_sfo_offsets_are_diagnosed_without_guessing(self):
        listing = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
        corrupt = bytearray(sfo("CUSA02343"))
        struct.pack_into("<I", corrupt, 8, 0xFFFF0000)
        inventory, diagnostic, _ = self.with_server(
            listing, {"CUSA02343": bytes(corrupt)})
        self.assertEqual(inventory["games"], [])
        self.assertIn("SFO CUSA02343: -32", diagnostic)
        self.assertFalse(inventory["scan_complete"])


    def test_ps4_playable_categories_are_accepted(self):
        listing = (
            b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
            b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA00411\r\n"
            b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA08032\r\n"
        )
        inventory, diagnostic, _ = self.with_server(
            listing, {
                "CUSA02343": sfo("CUSA02343", category="gda"),
                "CUSA00411": sfo("CUSA00411", category="gdo"),
                "CUSA08032": sfo("CUSA08032", category="gde"),
            })
        self.assertEqual(
            [g["cusa"] for g in inventory["games"]],
            ["CUSA02343", "CUSA00411", "CUSA08032"]
        )
        self.assertTrue(inventory["scan_complete"])

    def test_truncated_content_id_does_not_restore_missing_title_id(self):
        listing = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
        inventory, diagnostic, _ = self.with_server(
            listing, {"CUSA02343": sfo(
                "CUSA02343", include_title_id=False,
                content_id="IV0000-CUSA02343_")})
        self.assertEqual(inventory["games"], [])
        self.assertFalse(inventory["scan_complete"])
        self.assertIn("SFO CUSA02343: -33", diagnostic)

    def test_invalid_content_id_suffix_is_rejected(self):
        listing = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
        inventory, diagnostic, _ = self.with_server(
            listing, {"CUSA02343": sfo(
                "CUSA02343", content_id="IV0000-CUSA02343_00-0123456789ABC_EF")})
        self.assertEqual(inventory["games"], [])
        self.assertFalse(inventory["scan_complete"])
        self.assertIn("SFO CUSA02343: -35", diagnostic)

    def test_listing_over_capacity_cannot_claim_complete_scan(self):
        listing = b"".join(
            ("drwxr-xr-x root root 0 Nov 08 12:00 CUSA%05d\r\n" % i).encode()
            for i in (2343, 411, 8032)
        )
        metadata = {
            "CUSA02343": sfo("CUSA02343"),
            "CUSA00411": sfo("CUSA00411"),
            "CUSA08032": sfo("CUSA08032"),
        }
        inventory, diagnostic, _ = self.with_server(
            listing, metadata, executable=self.limited_executable)
        self.assertEqual(len(inventory["directory_candidates"]), 2)
        self.assertEqual(len(inventory["games"]), 2)
        self.assertFalse(inventory["scan_complete"])
        self.assertIn("LIST_TRUNCATED: 1", diagnostic)

    def test_combined_root_capacity_overflow_reports_incomplete_scan(self):
        internal = (
            b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
            b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA00411\r\n"
        )
        external = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA08032\r\n"
        def configure(server):
            server.external_listing = external
        inventory, diagnostic, _ = self.with_server(
            internal,
            {"CUSA02343": sfo("CUSA02343"),
             "CUSA00411": sfo("CUSA00411"),
             "CUSA08032": sfo("CUSA08032")},
            executable=self.limited_executable, passthrough=configure)
        self.assertFalse(inventory["scan_complete"])
        self.assertEqual(len(inventory["directory_candidates"]), 2)
        self.assertIn("CANDIDATE_CAPACITY_EXCEEDED: 1", diagnostic)

    def test_content_id_substring_cannot_impersonate_title(self):
        listing = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
        inventory, diagnostic, _ = self.with_server(
            listing, {
                "CUSA02343": sfo(
                    "CUSA02343", include_title_id=False,
                    content_id="IV0000-CUSA99999_00-CUSA02343XXXXXXX",
                )
            })
        self.assertEqual(inventory["games"], [])
        self.assertFalse(inventory["scan_complete"])
        self.assertIn("SFO CUSA02343: -33", diagnostic)


    def test_nine_realistic_candidates_remain_visible_when_sfo_invalid(self):
        ids = [
            "CUSA00001", "CUSA06122", "CUSA57220",
            "CUSA03506", "CUSA01116", "CUSA08032",
            "CUSA00411", "CUSA03551", "CUSA02343",
        ]
        listing = b"".join(
            ("drwxr-xr-x root root 0 Nov 08 12:00 " + title + "\r\n").encode()
            for title in ids
        )
        inventory, diagnostic, commands = self.with_server(
            listing, {title: b"BAD!" for title in ids})
        self.assertEqual(inventory["games"], [])
        self.assertEqual(
            [x["cusa"] for x in inventory["directory_candidates"]], ids
        )
        self.assertTrue(all(
            x["metadata_verified"] is False
            for x in inventory["directory_candidates"]
        ))
        self.assertFalse(inventory["scan_complete"])
        self.assertIn("Directory candidates: 9", diagnostic)
        self.assertIn("SFO invalid: 9", diagnostic)
        self.assertEqual(diagnostic.count("magic=0x21444142"), 9)
        self.assertEqual(commands.count("RETR"), 9)

    def test_utf8_title_truncation_preserves_valid_json(self):
        listing = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
        # SFO title length can exceed our inventory's fixed-size field.
        title = "A" * 190 + "\u20ac" * 20
        inventory, diagnostic, _ = self.with_server(
            listing, {"CUSA02343": sfo("CUSA02343", title)})
        self.assertEqual(len(inventory["games"]), 1)
        self.assertTrue(inventory["games"][0]["name"].startswith("A" * 190))
        self.assertLessEqual(
            len(inventory["games"][0]["name"].encode("utf8")), 191)
        self.assertTrue(inventory["scan_complete"])
        self.assertNotIn("SFO CUSA02343:", diagnostic)

    def test_game_name_with_quotes_produces_valid_json(self):
        listing = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
        inventory, _, commands = self.with_server(
            listing, {"CUSA02343": sfo("CUSA02343", 'Game "A" \\ Test')})
        self.assertEqual(inventory["games"][0]["name"], 'Game "A" \\ Test')
        self.assertTrue(set(commands).issubset(READ_ONLY_COMMANDS), commands)


if __name__ == "__main__":
    unittest.main()
