'use strict';

const assert = require('node:assert/strict');
const net = require('node:net');
const { test } = require('node:test');
const { parseReply, requestLog, createEngineLogView } = require('../engine-logs.cjs');

test('the engine log YAML keeps a grouped occurrence distinct from its summary', () => {
	const encoded = Buffer.from('line\n...\nend').toString('base64');
	const reply = parseReply(`ok\nSession: 12\nClear: 3\nEntries:\n  - Serial: 41\n    Message64: ${encoded}\n    GroupId: 2\nMore: false\n...\n`);
	assert.equal(reply.Session, '12');
	assert.deepEqual(reply.Entries, [{ Serial: '41', Message64: encoded, GroupId: '2' }]);
	assert.equal(Buffer.from(reply.Entries[0].Message64, 'base64').toString('utf8'), 'line\n...\nend');
	assert.throws(() => parseReply('ok\nSession: 1\n  Fake: yes\n...\n'), /Unexpected engine log YAML/);
});

test('a split loopback reply containing an encoded delimiter is read as one response', async () => {
	const server = net.createServer(socket => {
		let request = '';
		socket.on('data', chunk => {
			request += chunk.toString('utf8');
			if (request.includes('...\n')) {
				const encoded = Buffer.from('a\n...\nb').toString('base64');
				socket.write(`ok\nEntries:\n  - Serial: 7\n    Message64: ${encoded}\n`);
				setTimeout(() => socket.end('More: false\n...\n'), 5);
			}
		});
	});
	await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
	try {
		const result = await requestLog(server.address().port, 'log.since', 'After: 6\n');
		assert.equal(result.Entries[0].Serial, '7');
		assert.equal(result.More, 'false');
		assert.equal(Buffer.from(result.Entries[0].Message64, 'base64').toString('utf8'), 'a\n...\nb');
	} finally {
		await new Promise(resolve => server.close(resolve));
	}
});

test('oversized loopback replies are rejected', async () => {
	const sockets = new Set();
	const server = net.createServer(socket => {
		sockets.add(socket);
		socket.on('close', () => sockets.delete(socket));
		socket.end(`ok\nPayload: ${'a'.repeat(270000)}\n...\n`);
	});
	await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
	let timeout;
	try {
		await Promise.race([
			assert.rejects(requestLog(server.address().port, 'log.since'), /too large/),
			new Promise((_, reject) => { timeout = setTimeout(() => reject(new Error('oversized request hung')), 5000); })
		]);
	} finally {
		clearTimeout(timeout);
		for (const socket of sockets) {
			socket.destroy();
		}
		await new Promise(resolve => server.close(resolve));
	}
});

test('native editor view groups repeated errors, clears, and resets on engine restart', async () => {
	const encoded = text => Buffer.from(text).toString('base64');
	const occurrences = [
		{ serial: 1, groupId: 1, message: 'first object', level: 4 },
		{ serial: 2, groupId: 0, message: 'plain', level: 2 },
		{ serial: 3, groupId: 1, message: 'second object', level: 4 }
	];
	let session = 1;
	let clear = 0;
	let revision = 3;
	const server = net.createServer(socket => {
		let request = '';
		socket.on('data', chunk => {
			request += chunk.toString('utf8');
			if (!request.includes('\n...\n')) {
				return;
			}
			if (request.startsWith('log.clear\n')) {
				occurrences.length = 0;
				++clear;
				++revision;
				socket.end(`ok\nProtocol: 1\nSession: ${session}\nClear: ${clear}\nRevision: ${revision}\n...\n`);
				return;
			}
			if (request.startsWith('log.groups\n')) {
				socket.end(`ok\nProtocol: 1\nSession: ${session}\nClear: ${clear}\nGroupCount: 1\nGroups:\n  []\nMore: false\n...\n`);
				return;
			}
			const after = BigInt(/After: (\d+)/.exec(request)?.[1] || '0');
			const events = occurrences.filter(entry => BigInt(entry.serial) > after);
			const rows = events.map(entry => `  - Serial: ${entry.serial}\n    TimeMs: 1000\n    Level: ${entry.level}\n`
				+ `    Category64: ${encoded('script')}\n    Message64: ${encoded(entry.message)}\n    GroupId: ${entry.groupId}\n`
				+ (entry.groupId ? `    GroupKey64: ${encoded('probe:target2')}\n    GroupDisplay64: ${encoded('1970-01-01 00:00:01.000 [script] second object (2)###log_group')}\n    GroupLevel: 4\n    GroupCount: 2\n    GroupLatest: 3\n    GroupTimeMs: 1000\n` : '')).join('');
			const entryBlock = rows ? `Entries:\n${rows}` : 'Entries:\n  []\n';
			socket.end(`ok\nProtocol: 1\nSession: ${session}\nClear: ${clear}\nRevision: ${revision}\nGroupCount: ${occurrences.some(entry => entry.groupId) ? 1 : 0}\n`
				+ `FirstRetained: ${occurrences[0]?.serial || 0}\nLatest: ${occurrences.at(-1)?.serial || 0}\nReset: false\nGap: false\n${entryBlock}More: false\n...\n`);
		});
	});
	await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
	let provider;
	const commands = new Map();
	class FakeTreeItem {
		constructor(label, collapsibleState) {
			this.label = label;
			this.collapsibleState = collapsibleState;
		}
	}
	const api = {
		EventEmitter: class {
			event = () => ({ dispose() {} });
			fire() {}
			dispose() {}
		},
		TreeItem: FakeTreeItem,
		TreeItemCollapsibleState: { Collapsed: 1 },
		ThemeIcon: class { constructor(id) { this.id = id; } },
		window: { createTreeView(_id, options) { provider = options.treeDataProvider; return { message: '', dispose() {} }; } },
		workspace: {
			getConfiguration() { return { get() { return server.address().port; } }; },
			onDidChangeConfiguration() { return { dispose() {} }; }
		},
		commands: {
			registerCommand(id, run) { commands.set(id, run); return { dispose() { commands.delete(id); } }; },
			executeCommand() {}
		},
		l10n: { t: text => text }
	};
	const view = createEngineLogView(api);
	async function until(check) {
		for (let attempt = 0; attempt < 50; ++attempt) {
			const result = check();
			if (result) {
				return result;
			}
			await new Promise(resolve => setTimeout(resolve, 25));
		}
		throw new Error('engine log view did not update');
	}
	try {
		const roots = await until(() => provider.getChildren().length === 2 && provider.getChildren());
		const group = roots.find(row => row.kind === 'group');
		assert.ok(provider.getTreeItem(group).label.includes('second object (2)'));
		assert.deepEqual(provider.getChildren(group).map(row => row.entry?.message), ['second object', 'first object']);
		assert.equal(roots.filter(row => row.kind === 'entry').length, 1);
		await commands.get('jbro.engineLogs.clear')();
		assert.deepEqual(provider.getChildren(), []);
		session = 2;
		clear = 0;
		revision = 1;
		occurrences.push({ serial: 1, groupId: 0, message: 'new session', level: 3 });
		await until(() => provider.getChildren()[0]?.entry?.message === 'new session');
		assert.equal(provider.getChildren().length, 1, 'old grouped errors do not survive restart');
	} finally {
		view.dispose();
		await new Promise(resolve => server.close(resolve));
	}
});
