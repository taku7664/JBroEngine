// Runs the grammar assertion tests and snapshot tests through vscode-tmgrammar-test's library.
//
// Why not the tool's own commands:
// - Both commands call process.exit() while handles are still open. On Node 24 for Windows that
//   aborts in libuv, so a passing run exits with 0xC0000409 and a failing run with -1. The exit code
//   cannot tell them apart, and `npm test` would always fail.
// - The snapshot command writes a missing .snap file and reports success. A snapshot that was
//   never recorded would pass silently. Here a missing snapshot fails unless --update is given.
//
// Usage: node scripts/run-grammar-tests.mjs [--update]

import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const common = require('vscode-tmgrammar-test/dist/common/index.js');
const unit = require('vscode-tmgrammar-test/dist/unit/index.js');
const snapshot = require('vscode-tmgrammar-test/dist/snapshot/index.js');

const root = path.resolve(import.meta.dirname, '..');
const extensionDir = path.join(root, 'extensions', 'jbro-languages');
const grammarPath = path.join(extensionDir, 'syntaxes', 'jscript.tmLanguage.json');
const configPath = path.join(extensionDir, 'package.json');
const scopeName = 'source.jscript';
const syntaxDir = path.join(extensionDir, 'test', 'syntax');
const snapDir = path.join(extensionDir, 'test', 'snap');
const update = process.argv.includes('--update');

const failures = [];

function listJscript(dir) {
	return fs.readdirSync(dir)
		.filter(name => name.endsWith('.jscript'))
		.map(name => path.join(dir, name));
}

function normalizeNewlines(text) {
	return text.replace(/\r\n/g, '\n');
}

const { grammars } = common.loadConfiguration(configPath, scopeName, [grammarPath]);
const registry = common.createRegistry(grammars);

const syntaxFiles = listJscript(syntaxDir);
if (syntaxFiles.length === 0) {
	failures.push(`no syntax tests in ${syntaxDir}`);
}
for (const file of syntaxFiles) {
	const name = path.relative(root, file);
	const testCase = unit.parseGrammarTestCase(normalizeNewlines(fs.readFileSync(file, 'utf8')));
	const assertionCount = testCase.assertions.reduce((sum, line) => sum + line.scopeAssertions.length, 0);
	if (assertionCount === 0) {
		failures.push(`${name}: has no assertions - it would pass without checking anything`);
		continue;
	}
	const result = await unit.runGrammarTestCase(registry, testCase);
	for (const failure of result) {
		const where = `${name}:${failure.srcLine + 1}:${failure.start + 1}-${failure.end}`;
		if (failure.missing.length > 0) {
			failures.push(`${where} missing [${failure.missing.join(' ')}], actual [${failure.actual.join(' ')}]`);
		}
		if (failure.unexpected.length > 0) {
			failures.push(`${where} unexpected [${failure.unexpected.join(' ')}], actual [${failure.actual.join(' ')}]`);
		}
	}
	console.log(`syntax   ${name}: ${assertionCount} assertion(s), ${result.length} failure(s)`);
}

const snapFiles = listJscript(snapDir);
if (snapFiles.length === 0) {
	failures.push(`no snapshot tests in ${snapDir}`);
}
for (const file of snapFiles) {
	const name = path.relative(root, file);
	const snapPath = `${file}.snap`;
	const source = normalizeNewlines(fs.readFileSync(file, 'utf8'));
	const tokens = await snapshot.getVSCodeTokens(registry, scopeName, source);
	const rendered = normalizeNewlines(snapshot.renderSnap(tokens));
	if (update) {
		fs.writeFileSync(snapPath, rendered, 'utf8');
		console.log(`snapshot ${name}: written`);
		continue;
	}
	if (!fs.existsSync(snapPath)) {
		failures.push(`${name}: no .snap file - record it with --update and review it before committing`);
		continue;
	}
	const expected = normalizeNewlines(fs.readFileSync(snapPath, 'utf8'));
	if (expected === rendered) {
		console.log(`snapshot ${name}: matches`);
		continue;
	}
	const expectedLines = expected.split('\n');
	const renderedLines = rendered.split('\n');
	const count = Math.max(expectedLines.length, renderedLines.length);
	for (let i = 0; i < count; i++) {
		if (expectedLines[i] !== renderedLines[i]) {
			failures.push(`${name}.snap line ${i + 1} differs\n  expected: ${expectedLines[i] ?? '<none>'}\n  actual:   ${renderedLines[i] ?? '<none>'}`);
			break;
		}
	}
}

if (failures.length > 0) {
	for (const failure of failures) {
		console.error(`FAIL ${failure}`);
	}
	process.exitCode = 1;
} else {
	console.log('all grammar tests passed');
}
