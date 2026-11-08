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


def sfo(title_id, name="Test Game", version="01.04", category="gd"):
    fields = [
        ("TITLE_ID", title_id),
        ("TITLE", name),
        ("APP_VER", version),
        ("CONTENT_ID", "IV0000-" + title_id + "_00-SCANNER00000000"),
        ("CATEGORY", category),
    ]
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


class MockHandler(socketserver.StreamRequestHandler):
    def respond(self, text):
        self.wfile.write((text + "\r\n").encode("ascii"))
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
                            payload = b""
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
        self.commands = []
        self.pasv_advertisement = "127,0,0,1"
        self.deny_login = False
        self.require_login = False
        self.challenge_password = False
        self.type_unknown = False
        self.packet_strict = False


class ScannerFTPTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which("cc"):
            raise unittest.SkipTest("C compiler unavailable")
        cls.temp = tempfile.TemporaryDirectory()
        cls.executable = str(Path(cls.temp.name) / "ftp-scanner")
        subprocess.run(
            ["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2",
             "-o", cls.executable, str(SCANNER)],
            check=True, capture_output=True, text=True,
        )

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "temp"):
            cls.temp.cleanup()

    def run_client(self, port):
        with tempfile.TemporaryDirectory() as directory:
            inv = Path(directory) / "inventory.json"
            diagnostic = Path(directory) / "diagnostic.txt"
            env = dict(os.environ,
                       PS4_FTP_TEST_PORT=str(port),
                       PS4_FTP_TEST_OUT=str(inv),
                       PS4_FTP_TEST_REPORT=str(diagnostic))
            proc = subprocess.run(
                [self.executable], env=env, text=True,
                capture_output=True, timeout=20,
            )
            self.assertEqual(proc.returncode, 0, proc.stderr)
            return json.loads(inv.read_text()), diagnostic.read_text()

    def with_server(self, listing, metadata, *, passthrough=None):
        with MockFTP(listing, metadata) as server:
            if passthrough:
                passthrough(server)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                inventory, diagnostic = self.run_client(server.server_address[1])
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
        self.assertTrue(inventory["scan_complete"])
        self.assertEqual([g["cusa"] for g in inventory["games"]],
                         ["CUSA02343", "CUSA00411"])
        self.assertEqual(inventory["games"][0]["app_ver"], "01.07")
        self.assertEqual(inventory["games"][0]["install_state"], "UNKNOWN")
        self.assertFalse(inventory["download_enabled"])
        self.assertFalse(inventory["install_enabled"])
        self.assertIn("Metadata records: 2", diagnostic)
        self.assertIn("FTP_LOGIN_OK: 1", diagnostic)
        self.assertIn("FTP_GREETING_CODE: 220", diagnostic)
        self.assertIn("FTP_COMMAND_MODE (0=PASV-first, no TYPE): 0", diagnostic)
        self.assertEqual(commands[0], "PASV")
        self.assertNotIn("TYPE", commands)
        self.assertNotIn("USER", commands)
        self.assertNotIn("PASS", commands)
        self.assertTrue(set(commands).issubset(READ_ONLY_COMMANDS), commands)
        self.assertEqual(commands.count("RETR"), 3)

    def test_corrupt_sfo_is_not_counted(self):
        listing = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
        inventory, diagnostic, commands = self.with_server(
            listing, {"CUSA02343": b"not an SFO"})
        self.assertEqual(inventory["games"], [])
        self.assertFalse(inventory["scan_complete"])
        self.assertIn("SFO CUSA02343: -21", diagnostic)
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
        self.assertIn("SFO CUSA02343: -21", diagnostic)

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
        self.assertIn("FTP_LOGIN_OK: 0", diagnostic)
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

    def test_game_name_with_quotes_produces_valid_json(self):
        listing = b"drwxr-xr-x root root 0 Nov 08 12:00 CUSA02343\r\n"
        inventory, _, commands = self.with_server(
            listing, {"CUSA02343": sfo("CUSA02343", 'Game "A" \\ Test')})
        self.assertEqual(inventory["games"][0]["name"], 'Game "A" \\ Test')
        self.assertTrue(set(commands).issubset(READ_ONLY_COMMANDS), commands)


if __name__ == "__main__":
    unittest.main()
