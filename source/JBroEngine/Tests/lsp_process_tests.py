"""Real-process LSP acceptance tests. The server must not write project files."""
import argparse
import json
import pathlib
import queue
import subprocess
import tempfile
import threading
import time
import unittest

EXE = None


class Session:
    def __init__(self, project, locale="en-US", dynamic_watching=False, versioned_edits=True):
        self.process = subprocess.Popen([str(EXE), "--lsp"], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.messages = queue.Queue()
        self.notifications = []
        self.next_id = 1
        self.errors = bytearray()
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.error_reader = threading.Thread(target=self._errors, daemon=True)
        self.reader.start()
        self.error_reader.start()
        try:
            self._initialize(project, locale, dynamic_watching, versioned_edits)
        except BaseException:
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait(timeout=5)
            self._close_streams()
            raise

    def _initialize(self, project, locale, dynamic_watching, versioned_edits):
        self.initialized = self.request("initialize", {
            "processId": None, "rootUri": project.parent.as_uri(),
            "workspaceFolders": [{"uri": project.parent.as_uri(), "name": "project"}],
            "locale": locale,
            "initializationOptions": {"projectFile": str(project)},
            "capabilities": {
                "general": {"positionEncodings": ["utf-16"]},
                "workspace": {"workspaceEdit": {"documentChanges": versioned_edits},
                              "didChangeWatchedFiles": {"dynamicRegistration": dynamic_watching,
                                                        "relativePatternSupport": True}},
                "textDocument": {"publishDiagnostics": {"versionSupport": True}}
            }
        })
        self.notify("initialized", {})

    def _errors(self):
        while True:
            part = self.process.stderr.read(1)
            if not part:
                return
            self.errors.extend(part)

    def _read(self):
        try:
            while True:
                headers = {}
                while True:
                    line = self.process.stdout.readline()
                    if not line:
                        self.messages.put(EOFError(self.errors.decode("utf-8", "replace")))
                        return
                    if line == b"\r\n":
                        break
                    name, value = line.decode("ascii").split(":", 1)
                    headers[name.lower()] = value.strip()
                remaining = int(headers["content-length"])
                body = bytearray()
                while remaining:
                    chunk = self.process.stdout.read(remaining)
                    if not chunk:
                        raise EOFError("truncated JSON-RPC response")
                    body.extend(chunk)
                    remaining -= len(chunk)
                self.messages.put(json.loads(body))
        except BaseException as error:
            self.messages.put(error)

    def write(self, message):
        data = json.dumps(message, ensure_ascii=False).encode("utf-8")
        self.process.stdin.write(b"Content-Length: " + str(len(data)).encode("ascii") + b"\r\n\r\n" + data)
        self.process.stdin.flush()

    def notify(self, method, params):
        self.write({"jsonrpc": "2.0", "method": method, "params": params})

    def request(self, method, params, timeout=15, cancel=False):
        request_id = self.next_id
        self.next_id += 1
        self.write({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params})
        if cancel:
            self.notify("$/cancelRequest", {"id": request_id})
        deadline = time.monotonic() + timeout
        while True:
            response = self.messages.get(timeout=max(0.01, deadline - time.monotonic()))
            if isinstance(response, BaseException):
                raise response
            if response.get("id") == request_id and ("result" in response or "error" in response):
                return response
            self.notifications.append(response)

    def diagnostics(self, uri, version=None, timeout=15):
        def matches(message):
            if message.get("method") != "textDocument/publishDiagnostics":
                return False
            params = message["params"]
            return params["uri"] == uri and (version is None or params.get("version") == version)
        deadline = time.monotonic() + timeout
        while True:
            for index, message in enumerate(self.notifications):
                if matches(message):
                    return self.notifications.pop(index)["params"]["diagnostics"]
            message = self.messages.get(timeout=max(0.01, deadline - time.monotonic()))
            if isinstance(message, BaseException):
                raise message
            self.notifications.append(message)

    def close(self):
        if self.process.poll() is None:
            try:
                response = self.request("shutdown", {})
                if "error" in response:
                    raise AssertionError(response)
                self.notify("exit", {})
                self.process.wait(timeout=5)
            except BaseException:
                self.process.kill()
                self.process.wait(timeout=5)
                raise
        self._close_streams()

    def _close_streams(self):
        self.reader.join(timeout=1)
        self.error_reader.join(timeout=1)
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            if not stream.closed:
                stream.close()


def position(source, needle, occurrence=0, advance=0):
    start = -1
    for _ in range(occurrence + 1):
        start = source.index(needle, start + 1)
    start += advance
    prefix = source[:start]
    return {"line": prefix.count("\n"),
            "character": len(prefix.rsplit("\n", 1)[-1].encode("utf-16-le")) // 2}


def location_params(uri, source, needle, occurrence=0, advance=0):
    return {"textDocument": {"uri": uri},
            "position": position(source, needle, occurrence, advance)}


class ProcessTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="JBro-LSP-")
        self.root = pathlib.Path(self.temp.name) / "한글 프로젝트"
        self.scripts = self.root / "Contents" / "Scripts"
        self.scripts.mkdir(parents=True)
        self.project = self.root / "Game.jproject"
        self.project.write_text("EngineVersion: 0.1.0\nFramework: 2D\nScriptSourceDirectory: Contents\n",
                                encoding="utf-8")
        self.sessions = []

    def tearDown(self):
        for session in self.sessions:
            session.close()
        self.temp.cleanup()

    def start(self, locale="en-US"):
        session = Session(self.project, locale)
        self.sessions.append(session)
        self.assertIn("result", session.initialized, session.initialized)
        return session

    def document(self, name, source):
        path = self.scripts / name
        path.write_text(source, encoding="utf-8", newline="")
        return path.as_uri()

    def open(self, session, uri, source, version=1):
        session.notify("textDocument/didOpen", {
            "textDocument": {"uri": uri, "languageId": "jbroscript", "version": version, "text": source}})
        return session.diagnostics(uri, version)

    def result(self, session, method, params):
        reply = session.request(method, params)
        self.assertIn("result", reply, reply)
        return reply["result"]

    def test_capabilities_and_shutdown(self):
        session = self.start()
        caps = session.initialized["result"]["capabilities"]
        self.assertEqual(caps["positionEncoding"], "utf-16")
        for key in ("completionProvider", "definitionProvider", "typeDefinitionProvider",
                    "documentSymbolProvider", "workspaceSymbolProvider", "hoverProvider",
                    "signatureHelpProvider", "referencesProvider", "renameProvider",
                    "semanticTokensProvider", "foldingRangeProvider", "documentFormattingProvider"):
            self.assertTrue(caps.get(key), key)

    def test_exit_does_not_wait_for_stdin_eof(self):
        session = self.start()
        self.assertIn("result", session.request("shutdown", {}))
        session.notify("exit", {})
        # Keep the writer open while waiting; closing stdin would conceal a blocked reader join.
        session.process.wait(timeout=5)
        self.assertEqual(session.process.returncode, 0)
        session.process.stdin.close()

    def test_malformed_exit_does_not_stop_reader(self):
        session = self.start()
        response = session.request("exit", {})
        self.assertEqual(response["error"]["code"], -32600)
        self.assertEqual(self.result(session, "workspace/symbol", {"query": "not_present"}), [])
        session.write({"jsonrpc": "invalid", "method": "exit", "params": {}})
        invalid = session.messages.get(timeout=5)
        self.assertEqual(invalid["error"]["code"], -32600)
        self.assertEqual(self.result(session, "workspace/symbol", {"query": "not_present"}), [])

    def test_dynamic_file_watch_registration(self):
        session = Session(self.project, dynamic_watching=True)
        self.sessions.append(session)
        self.assertIn("result", session.initialized)
        registration = session.messages.get(timeout=5)
        self.assertEqual(registration["method"], "client/registerCapability")
        watchers = registration["params"]["registrations"][0]
        self.assertEqual(watchers["method"], "workspace/didChangeWatchedFiles")
        self.assertEqual(watchers["registerOptions"]["watchers"][0]["kind"], 7)
        session.write({"jsonrpc": "2.0", "id": registration["id"], "result": None})
        self.assertEqual(self.result(session, "workspace/symbol", {"query": "not_present"}), [])

    def test_unsupported_project_is_rejected(self):
        for framework, version, expected in (("3D", "0.1.0", "3D"), ("2D", "999.0.0", "EngineVersion")):
            with self.subTest(framework=framework, version=version):
                self.project.write_text(f"EngineVersion: {version}\nFramework: {framework}\n", encoding="utf-8")
                frames = bytearray()
                for message in ({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                                 "params": {"initializationOptions": {"projectFile": str(self.project)}}},
                                {"jsonrpc": "2.0", "method": "exit", "params": {}}):
                    body = json.dumps(message, ensure_ascii=False).encode("utf-8")
                    frames.extend(b"Content-Length: " + str(len(body)).encode("ascii") + b"\r\n\r\n" + body)
                process = subprocess.run([str(EXE), "--lsp"], input=bytes(frames),
                                         capture_output=True, timeout=15)
                self.assertEqual(process.returncode, 1)
                header, body = process.stdout.split(b"\r\n\r\n", 1)
                self.assertTrue(header.startswith(b"Content-Length: "))
                response = json.loads(body)
                self.assertEqual(response["id"], 1)
                self.assertEqual(response["error"]["code"], -32602)
                self.assertIn(expected, response["error"]["message"])

    def test_unsaved_diagnostics_fix_and_close(self):
        valid = "script Enemy\n{\npublic Int hp = 1\n}\n"
        uri = self.document("Enemy.jscript", valid)
        session = self.start()
        invalid = valid.replace("= 1", '= "bad"')
        self.assertTrue(self.open(session, uri, invalid))
        session.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2},
                                                 "contentChanges": [{"text": valid}]})
        self.assertEqual(session.diagnostics(uri, 2), [])
        session.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 3},
                                                 "contentChanges": [{"text": invalid}]})
        self.assertTrue(session.diagnostics(uri, 3))
        session.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
        self.assertEqual(session.diagnostics(uri), [])
        self.assertEqual((self.scripts / "Enemy.jscript").read_text(encoding="utf-8"), valid)

    def test_cross_file_definition_symbols_and_references(self):
        stats = "struct Stats\n{\npublic Int hp = 1\n}\n"
        enemy = "script Enemy\n{\npublic Stats stats\n}\n"
        stats_uri = self.document("Stats.jscript", stats)
        enemy_uri = self.document("Enemy.jscript", enemy)
        session = self.start()
        self.assertEqual(self.open(session, enemy_uri, enemy), [])
        definitions = self.result(session, "textDocument/definition",
                                  location_params(enemy_uri, enemy, "Stats"))
        if isinstance(definitions, dict):
            definitions = [definitions]
        self.assertTrue(any(entry.get("uri", entry.get("targetUri")) == stats_uri for entry in definitions))
        typed = self.result(session, "textDocument/typeDefinition", location_params(enemy_uri, enemy, "stats"))
        self.assertEqual(typed["uri"], stats_uri)
        symbols = self.result(session, "textDocument/documentSymbol", {"textDocument": {"uri": enemy_uri}})
        self.assertTrue(any(item["name"] == "Enemy" for item in symbols))
        workspace = self.result(session, "workspace/symbol", {"query": "Stats"})
        self.assertTrue(any(item["name"] == "Stats" for item in workspace))
        references = self.result(session, "textDocument/references", {
            **location_params(enemy_uri, enemy, "Stats"), "context": {"includeDeclaration": True}})
        self.assertGreaterEqual(len(references), 2)

    def test_bom_disk_declaration_uses_visible_utf16_columns(self):
        stats = "\ufeffstruct Stats\n{\npublic Int hp = 1\n}\n"
        enemy = "script Enemy\n{\npublic Stats stats\n}\n"
        stats_uri = self.document("Stats.jscript", stats)
        enemy_uri = self.document("Enemy.jscript", enemy)
        session = self.start()
        self.assertEqual(self.open(session, enemy_uri, enemy), [])
        definition = self.result(session, "textDocument/definition", location_params(enemy_uri, enemy, "Stats"))
        if isinstance(definition, list):
            definition = definition[0]
        self.assertEqual(definition["uri"], stats_uri)
        self.assertEqual(definition["range"]["start"], {"line": 0, "character": 7})
        self.assertEqual(definition["range"]["end"], {"line": 0, "character": 12})

    def test_scope_completion_hover_and_signature(self):
        source = ("script Enemy\n{\npublic Int hp = 1\n"
                  "fn Add(Int amount) -> Int\n{\nreturn hp + amount\n}\n"
                  "fn OnUpdate() callback\n{\nInt result = Add(2)\nhp = result\n}\n}\n")
        uri = self.document("Enemy.jscript", source)
        session = self.start()
        self.assertEqual(self.open(session, uri, source), [])
        hover = self.result(session, "textDocument/hover", location_params(uri, source, "Add(2)"))
        self.assertIsNotNone(hover)
        signature = self.result(session, "textDocument/signatureHelp",
                                location_params(uri, source, "Add(2)", advance=4))
        self.assertTrue(signature["signatures"])
        completion = self.result(session, "textDocument/completion",
                                 location_params(uri, source, "hp = result", advance=2))
        items = completion["items"] if isinstance(completion, dict) else completion
        labels = {item["label"] for item in items}
        self.assertIn("hp", labels)
        self.assertNotIn("amount", labels, "parameter from another function must not leak")

    def test_rename_edits_and_collision(self):
        source = "script Enemy\n{\npublic Int hp = 1\npublic Int mp = 2\nfn OnUpdate() callback\n{\nhp += 1\n}\n}\n"
        uri = self.document("Enemy.jscript", source)
        session = self.start()
        self.assertEqual(self.open(session, uri, source), [])
        prepared = self.result(session, "textDocument/prepareRename", location_params(uri, source, "hp"))
        self.assertEqual(prepared["range"]["start"], position(source, "hp"))
        edit = self.result(session, "textDocument/rename",
                           {**location_params(uri, source, "hp"), "newName": "health"})
        self.assertTrue(edit.get("documentChanges"))
        changes = edit["documentChanges"][0]
        self.assertEqual(changes["textDocument"]["version"], 1)
        self.assertEqual(changes["textDocument"]["uri"], uri)
        self.assertEqual(len(changes["edits"]), 2)
        self.assertTrue(all(item["newText"] == "health" for item in changes["edits"]))
        rejected = session.request("textDocument/rename",
                                   {**location_params(uri, source, "hp"), "newName": "mp"})
        self.assertIn("error", rejected)
        self.assertEqual((self.scripts / "Enemy.jscript").read_text(encoding="utf-8"), source)

    def test_rename_requires_versioned_edits(self):
        source = "script Enemy\n{\npublic Int hp = 1\n}\n"
        uri = self.document("Enemy.jscript", source)
        session = Session(self.project, versioned_edits=False)
        self.sessions.append(session)
        self.assertFalse(session.initialized["result"]["capabilities"].get("renameProvider"))
        self.assertEqual(self.open(session, uri, source), [])
        response = session.request("textDocument/rename", {**location_params(uri, source, "hp"), "newName": "health"})
        self.assertIn("error", response, "a rename must never omit open-document version guards")

    def test_class_rename_updates_constructor_declarations_and_calls(self):
        source = ("class Box\n{\npublic fn Box(Int value)\n{\n}\n}\nclass User\n{\n"
                  "fn F()\n{\nBox item = Box(1)\n}\n}\n")
        original = source
        uri = self.document("ConstructorRename.jscript", source)
        session = self.start()
        self.assertEqual(self.open(session, uri, source), [])
        for version, (needle, new_name) in enumerate(
                [("Box", "Crate"), ("Crate(1)", "Parcel"), ("Parcel(Int", "Packet")], start=1):
            edit = self.result(session, "textDocument/rename",
                               {**location_params(uri, source, needle), "newName": new_name})
            self.assertEqual(len(edit["documentChanges"]), 1)
            change = edit["documentChanges"][0]
            self.assertEqual(change["textDocument"], {"uri": uri, "version": version})
            lines = source.splitlines(keepends=True)
            def offset(point):
                return sum(map(len, lines[:point["line"]])) + point["character"]
            edits = [(offset(item["range"]["start"]), offset(item["range"]["end"]), item["newText"])
                     for item in change["edits"]]
            self.assertEqual(len(edits), 4, "type, constructor declaration, variable type and call must move together")
            self.assertEqual(len({(start, end) for start, end, _ in edits}), len(edits))
            for start, end, text in sorted(edits, reverse=True):
                source = source[:start] + text + source[end:]
            session.notify("textDocument/didChange", {
                "textDocument": {"uri": uri, "version": version + 1},
                "contentChanges": [{"text": source}]})
            self.assertEqual(session.diagnostics(uri, version + 1), [], source)
        self.assertEqual((self.scripts / "ConstructorRename.jscript").read_text(encoding="utf-8"), original)

    def test_function_rename_updates_override_family_and_calls(self):
        source = ("class Base\n{\npublic fn Run()\n{\n}\n}\n"
                  "class Child : Base\n{\npublic fn Run() override\n{\n}\n"
                  "fn Call()\n{\nRun()\n}\n}\n")
        original = source
        uri = self.document("OverrideRename.jscript", source)
        session = self.start()
        self.assertEqual(self.open(session, uri, source), [])
        for version, (name, occurrence, new_name) in enumerate(
                [("Run", 0, "Execute"), ("Execute", 1, "Perform"), ("Perform", 2, "Act")], start=1):
            edit = self.result(session, "textDocument/rename", {
                **location_params(uri, source, name, occurrence), "newName": new_name})
            self.assertEqual(len(edit["documentChanges"]), 1)
            change = edit["documentChanges"][0]
            self.assertEqual(change["textDocument"], {"uri": uri, "version": version})
            lines = source.splitlines(keepends=True)
            def offset(point):
                return sum(map(len, lines[:point["line"]])) + point["character"]
            edits = [(offset(item["range"]["start"]), offset(item["range"]["end"]), item["newText"])
                     for item in change["edits"]]
            self.assertEqual(len(edits), 3, "base, override and call must move together")
            self.assertEqual(len({(start, end) for start, end, _ in edits}), len(edits))
            for start, end, text in sorted(edits, reverse=True):
                source = source[:start] + text + source[end:]
            session.notify("textDocument/didChange", {
                "textDocument": {"uri": uri, "version": version + 1},
                "contentChanges": [{"text": source}]})
            self.assertEqual(session.diagnostics(uri, version + 1), [], source)
        self.assertEqual((self.scripts / "OverrideRename.jscript").read_text(encoding="utf-8"), original)

    def test_utf16_emoji_and_incremental_change(self):
        source = 'script Enemy\n{\nfn OnUpdate() callback\n{\nString label = "😀" + missing\n}\n}\n'
        uri = self.document("Enemy.jscript", source)
        session = self.start()
        diagnostics = self.open(session, uri, source)
        expected = position(source, "missing")
        self.assertTrue(any(item["range"]["start"] == expected for item in diagnostics), diagnostics)
        session.notify("textDocument/didChange", {
            "textDocument": {"uri": uri, "version": 2},
            "contentChanges": [{"range": {"start": expected,
                                         "end": {**expected, "character": expected["character"] + 7}},
                                "text": '"ok"'}]})
        self.assertEqual(session.diagnostics(uri, 2), [])

    def test_semantic_tokens_folding_and_formatting(self):
        source = "script Enemy\r\n{\r\npublic Int hp = 1\r\nfn OnUpdate() callback\r\n{\r\n// retain { comment\r\nhp += 1\r\n}\r\n}\r\n"
        uri = self.document("Enemy.jscript", source)
        session = self.start()
        self.assertEqual(self.open(session, uri, source), [])
        tokens = self.result(session, "textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})
        self.assertTrue(tokens["data"])
        self.assertEqual(len(tokens["data"]) % 5, 0)
        folds = self.result(session, "textDocument/foldingRange", {"textDocument": {"uri": uri}})
        self.assertTrue(any(item["endLine"] > item["startLine"] for item in folds))
        edits = self.result(session, "textDocument/formatting",
                            {"textDocument": {"uri": uri}, "options": {"tabSize": 4, "insertSpaces": True}})
        self.assertTrue(edits)
        self.assertIn("        hp += 1\r\n", edits[0]["newText"])
        self.assertIn("// retain { comment", edits[0]["newText"])
        self.assertEqual((self.scripts / "Enemy.jscript").read_bytes(), source.encode("utf-8"))

    def test_engine_completion_nullable_and_readonly(self):
        source = ("script Enemy\n{\npublic Transform2D? target\n"
                  "fn OnUpdate() callback\n{\nif (target is not null)\n{\n"
                  "target.worldPosition = Vector2()\n}\n}\n}\n")
        uri = self.document("Enemy.jscript", source)
        session = self.start()
        diagnostics = self.open(session, uri, source)
        self.assertTrue(diagnostics, "write to worldPosition must be rejected")
        completion = self.result(session, "textDocument/completion",
                                 location_params(uri, source, "target.worldPosition", advance=7))
        items = completion["items"] if isinstance(completion, dict) else completion
        self.assertTrue(any(item["label"] == "worldPosition" for item in items), items)

    def test_unknown_method_does_not_poison_session(self):
        session = self.start()
        response = session.request("test/unknownMethod", {})
        self.assertEqual(response["error"]["code"], -32601)
        self.assertEqual(self.result(session, "workspace/symbol", {"query": "not_present"}), [])

    def test_dependency_change_delete_and_recreate(self):
        stats = "struct Stats\n{\npublic Int hp = 1\n}\n"
        enemy = "script Enemy\n{\npublic Stats stats\nfn OnUpdate() callback\n{\nInt amount = stats.hp\n}\n}\n"
        stats_uri = self.document("Stats.jscript", stats)
        enemy_uri = self.document("Enemy.jscript", enemy)
        session = self.start()
        self.assertEqual(self.open(session, enemy_uri, enemy), [])
        path = self.scripts / "Stats.jscript"
        path.write_text(stats.replace("Int hp = 1", 'String hp = "bad"'), encoding="utf-8")
        session.notify("workspace/didChangeWatchedFiles", {"changes": [{"uri": stats_uri, "type": 2}]})
        self.assertTrue(session.diagnostics(enemy_uri, 1), "dependency type edit must recheck the open document")
        path.unlink()
        session.notify("workspace/didChangeWatchedFiles", {"changes": [{"uri": stats_uri, "type": 3}]})
        workspace = self.result(session, "workspace/symbol", {"query": "Stats"})
        self.assertFalse(any(item["name"] == "Stats" for item in workspace))
        self.assertTrue(session.diagnostics(enemy_uri, 1), "removed type must produce an error")
        path.write_text(stats, encoding="utf-8")
        session.notify("workspace/didChangeWatchedFiles", {"changes": [{"uri": stats_uri, "type": 1}]})
        self.assertEqual(session.diagnostics(enemy_uri, 1), [], "recreated type clears the dependent error")

    def test_old_versions_do_not_replace_unsaved_text(self):
        source = "script Enemy\n{\npublic Int health = 1\n}\n"
        uri = self.document("Enemy.jscript", source)
        session = self.start()
        self.assertEqual(self.open(session, uri, source, version=5), [])
        session.notify("textDocument/didChange", {
            "textDocument": {"uri": uri, "version": 4},
            "contentChanges": [{"text": source.replace("health", "stale")}]
        })
        symbols = self.result(session, "workspace/symbol", {"query": "health"})
        self.assertTrue(any(item["name"] == "health" for item in symbols))
        self.assertEqual(self.result(session, "workspace/symbol", {"query": "stale"}), [])

    def test_cancelled_request_keeps_session_usable(self):
        source = "script Many\n{\n" + "".join(f"public Int field{index} = {index}\n" for index in range(2000)) + "}\n"
        uri = self.document("Many.jscript", source)
        session = self.start()
        session.notify("textDocument/didOpen", {"textDocument": {
            "uri": uri, "languageId": "jbroscript", "version": 1, "text": source}})
        response = session.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}}, cancel=True)
        self.assertEqual(response.get("error", {}).get("code"), -32800, response)
        self.assertTrue(self.result(session, "workspace/symbol", {"query": "Many"}))

    def test_native_api_definition_and_rename_protection(self):
        source = "script Enemy\n{\nfn OnUpdate() callback\n{\nInt value = Random.Range(1, 4)\n}\n}\n"
        uri = self.document("Enemy.jscript", source)
        session = self.start()
        self.assertEqual(self.open(session, uri, source), [])
        where = location_params(uri, source, "Range")
        definition = self.result(session, "textDocument/definition", where)
        self.assertTrue(definition["uri"].endswith("RandomService.h"), definition)
        self.assertGreater(definition["range"]["end"]["character"], definition["range"]["start"]["character"])
        rejected = session.request("textDocument/rename", {**where, "newName": "Roll"})
        self.assertIn("error", rejected, "native SDK definitions are read-only")

    def test_native_overloads_and_output_projections(self):
        source = ("script Player\n{\nfn OnUpdate() callback\n{\n"
                  "Vector2 origin = Vector2(1.0, 2.0)\n"
                  "RaycastHit2D? hit = Physics.Raycast(origin, Vector2(0.0, 1.0), 10.0)\n"
                  "if (hit is not null)\n{\nVector2 contact = hit.point\n}\n"
                  'String text = Save.ReadText("key") ?? "fallback"\n'
                  'Array<GameObject?> matches = Objects.FindAll("Enemy")\n'
                  "InputCapture capture = Input.CaptureBinding()\n"
                  "InputCaptureResult status = capture.result\n"
                  "InputBinding binding = capture.binding\n"
                  "Int roll = Random.Range(1, 4)\nFloat fraction = Random.Range(0.0, 1.0)\n}\n}\n")
        uri = self.document("Player.jscript", source)
        session = self.start()
        self.assertEqual(self.open(session, uri, source), [])
        signature = self.result(session, "textDocument/signatureHelp",
                                location_params(uri, source, "Physics.Raycast(", advance=len("Physics.Raycast(")))
        self.assertTrue(signature["signatures"])
        label = signature["signatures"][0]["label"]
        self.assertIn("RaycastHit2D?", label)
        self.assertNotIn("ref ", label, "native output parameter is returned to the script")

    def test_incomplete_code_completion_and_invalid_formatting(self):
        source = "script Enemy\n{\npublic Transform2D? target\nfn OnUpdate() callback\n{\ntarget!.\n}\n}\n"
        uri = self.document("Enemy.jscript", source)
        session = self.start()
        self.assertTrue(self.open(session, uri, source))
        completion = self.result(session, "textDocument/completion",
                                 location_params(uri, source, "target!.", advance=len("target!.")))
        self.assertTrue(any(item["label"] == "position" for item in completion["items"]), completion)
        formatted = self.result(session, "textDocument/formatting", {
            "textDocument": {"uri": uri}, "options": {"tabSize": 4, "insertSpaces": True}})
        self.assertEqual(formatted, [], "syntax errors must not receive formatting edits")

    def test_common_math_values_are_available_in_2d_project(self):
        source = ("script Values\n{\nfn OnUpdate() callback\n{\n"
                  "Vector3 point = Vector3(1.0, 2.0, 3.0)\n"
                  "Vector4 plane = Vector4(1.0, 2.0, 3.0, 4.0)\n"
                  "Quaternion rotation = Quaternion()\n"
                  "Matrix4x4 matrix = Matrix4x4()\n"
                  "Vector3 shifted = Add(point, Vector3(1.0, 0.0, 0.0))\n"
                  "Float weight = plane.w + rotation.w\n"
                  "Matrix4x4 multiplied = MultiplyMatrix4x4(matrix, matrix)\n}\n}\n")
        uri = self.document("Values.jscript", source)
        session = self.start()
        self.assertEqual(self.open(session, uri, source), [])
        self.assertIsNotNone(self.result(session, "textDocument/hover", location_params(uri, source, "MultiplyMatrix4x4")))

    def test_overloads_and_nullable_flow(self):
        source = ("script Enemy\n{\npublic GameObject? target\n"
                  "fn Pick(Int amount) -> Int\n{\nreturn amount\n}\n"
                  "fn Pick(String label) -> String\n{\nreturn label\n}\n"
                  "fn Clear()\n{\ntarget = null\n}\n"
                  "fn OnUpdate() callback\n{\nInt amount = Pick(1)\n"
                  "if (target is not null)\n{\nClear()\ntarget.SetActive(true)\n}\n}\n}\n")
        uri = self.document("Enemy.jscript", source)
        session = self.start()
        diagnostics = self.open(session, uri, source)
        invalidated_line = position(source, "target.SetActive")["line"]
        self.assertTrue(any(item["range"]["start"]["line"] == invalidated_line for item in diagnostics), diagnostics)
        self.assertFalse(any("duplicate" in item["message"].lower() for item in diagnostics), diagnostics)

    def test_localization_is_selected_by_initialize(self):
        source = 'script Enemy\n{\npublic Int hp = "bad"\n}\n'
        uri = self.document("Enemy.jscript", source)
        english = self.start("en-US")
        english_diags = self.open(english, uri, source)
        korean = self.start("ko-KR")
        korean_diags = self.open(korean, uri, source)
        self.assertTrue(english_diags and korean_diags)
        self.assertNotEqual(english_diags[0]["message"], korean_diags[0]["message"])

    def test_nullable_member_can_be_assigned_null_after_narrowing(self):
        source = ("class Box\n{\npublic GameObject? target\n}\nscript S\n{\nBox box\n"
                  "fn OnUpdate() callback\n{\nif (box.target is not null)\n{\n"
                  "box.target = null\n}\n}\n}\n")
        uri = self.document("NullableMember.jscript", source)
        session = self.start()
        self.assertEqual(self.open(session, uri, source), [])

    def test_unreachable_return_after_break_does_not_satisfy_return_path(self):
        source = ("class C\n{\nfn F() -> Int\n{\nswitch (1)\n{\ndefault\n{\n"
                  "break\nreturn 1\n}\n}\n}\n}\n")
        uri = self.document("UnreachableReturn.jscript", source)
        session = self.start()
        diagnostics = self.open(session, uri, source)
        self.assertTrue(any(item["code"] == "JBC3011" for item in diagnostics), diagnostics)

    def test_remove_at_swap_cannot_change_container_size_during_iteration(self):
        source = ("class C\n{\nArray<Int> values\nfn F()\n{\nfor (item in values)\n{\n"
                  "values.RemoveAtSwap(0)\n}\n}\n}\n")
        uri = self.document("IterationMutation.jscript", source)
        session = self.start()
        diagnostics = self.open(session, uri, source)
        self.assertTrue(any(item["code"] == "JBC3019" for item in diagnostics), diagnostics)

    def test_switch_break_preserves_nullable_exit_path(self):
        source = ("script S\n{\nGameObject? target\nfn OnUpdate() callback\n{\n"
                  "switch (1)\n{\ndefault\n{\nif (target is null)\n{\nbreak\n}\n}\n}\n"
                  "target.SetActive(true)\n}\n}\n")
        uri = self.document("BreakExitFlow.jscript", source)
        session = self.start()
        diagnostics = self.open(session, uri, source)
        access_line = position(source, "target.SetActive")["line"]
        self.assertTrue(any(item["code"] == "JBC3007" and
                            item["range"]["start"]["line"] == access_line
                            for item in diagnostics), diagnostics)

    def test_continue_carries_nullable_assignment_to_next_iteration(self):
        source = ("script S\n{\nTransform2D? target\nBool running = true\nBool skip = true\n"
                  "fn OnUpdate() callback\n{\nif (target is not null)\n{\nwhile (running)\n{\n"
                  "if (skip)\n{\ntarget = null\ncontinue\n}\nVector2 point = target.position\n"
                  "}\n}\n}\n}\n")
        uri = self.document("ContinueFlow.jscript", source)
        session = self.start()
        diagnostics = self.open(session, uri, source)
        access_line = position(source, "target.position")["line"]
        self.assertTrue(any(item["code"] == "JBC3007" and
                            item["range"]["start"]["line"] == access_line
                            for item in diagnostics), diagnostics)

    def test_loop_break_carries_nullable_assignment_to_exit(self):
        source = ("script S\n{\nTransform2D? target\nBool running = true\nBool stop = true\n"
                  "fn OnUpdate() callback\n{\nif (target is not null)\n{\nwhile (running)\n{\n"
                  "if (stop)\n{\ntarget = null\nbreak\n}\n}\nVector2 point = target.position\n"
                  "}\n}\n}\n")
        uri = self.document("LoopBreakExit.jscript", source)
        session = self.start()
        diagnostics = self.open(session, uri, source)
        access_line = position(source, "target.position")["line"]
        self.assertTrue(any(item["code"] == "JBC3007" and
                            item["range"]["start"]["line"] == access_line
                            for item in diagnostics), diagnostics)

    def test_while_condition_is_checked_again_on_reachable_back_edges(self):
        for name, condition, jump, unsafe in (
                ("fallthrough", "target.valid", "", True),
                ("continue", "target.valid", "continue\n", True),
                ("guarded", "target is not null and target.valid", "", False),
                ("break", "target.valid", "break\n", False),
                ("return", "target.valid", "return\n", False)):
            with self.subTest(name=name):
                source = ("script S\n{\nGameObject? target\nfn OnUpdate() callback\n{\n"
                          "if (target is not null)\n{\nwhile (" + condition + ")\n{\n"
                          "target = null\n" + jump + "}\n}\n}\n}\n")
                uri = self.document("ConditionRecheck.jscript", source)
                session = self.start()
                diagnostics = self.open(session, uri, source)
                if unsafe:
                    condition_line = position(source, "while (")["line"]
                    self.assertTrue(any(item["code"] == "JBC3007" and
                                        item["range"]["start"]["line"] == condition_line
                                        for item in diagnostics), diagnostics)
                else:
                    self.assertEqual(diagnostics, [])

    def test_loop_checks_null_flow_until_it_stops_changing(self):
        source = ("script S\n{\nTransform2D? a\nTransform2D? b\nTransform2D? c\n"
                  "Bool running = true\nfn OnUpdate() callback\n{\n"
                  "if (a is not null and b is not null and c is not null)\n{\nwhile (running)\n{\n"
                  "Vector2 point = a.position\na = b\nb = c\nc = null\n}\n}\n}\n}\n")
        uri = self.document("FixedPointFlow.jscript", source)
        session = self.start()
        diagnostics = self.open(session, uri, source)
        access_line = position(source, "a.position")["line"]
        self.assertTrue(any(item["code"] == "JBC3007" and
                            item["range"]["start"]["line"] == access_line
                            for item in diagnostics), diagnostics)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=pathlib.Path)
    args, remaining = parser.parse_known_args()
    EXE = args.exe.resolve()
    if not EXE.is_file():
        parser.error(f"server executable not found: {EXE}")
    unittest.main(argv=["lsp_process_tests.py", *remaining])
