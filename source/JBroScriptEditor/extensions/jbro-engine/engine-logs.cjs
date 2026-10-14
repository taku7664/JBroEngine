'use strict';

const net = require('node:net');

const MAX_REPLY_BYTES = 256 * 1024;
const POLL_MS = 700;

// The engine control port emits a deliberately small YAML subset. Reject any
// unexpected shape instead of treating log text as protocol syntax.
function parseReply(raw) {
	const lines = raw.replace(/\r\n/g, '\n').split('\n');
	if (lines[0]?.startsWith('error: ')) {
		throw new Error(lines[0].slice(7));
	}
	if (lines[0] !== 'ok' || !lines.includes('...')) {
		throw new Error('Invalid engine log reply');
	}
	const result = {};
	let collection;
	let item;
	for (const line of lines.slice(1)) {
		if (line === '...') {
			break;
		}
		if (!line) {
			continue;
		}
		let match = /^([A-Za-z][A-Za-z0-9]*):(?: (.*))?$/.exec(line);
		if (match) {
			const [, key, value] = match;
			if (value === undefined && (key === 'Entries' || key === 'Groups')) {
				result[key] = [];
				collection = result[key];
				item = undefined;
			} else {
				result[key] = value === '""' ? '' : value;
				collection = undefined;
			}
			continue;
		}
		if (line === '  []' && collection) {
			continue;
		}
		match = /^  - ([A-Za-z][A-Za-z0-9]*):(?: (.*))?$/.exec(line);
		if (match && collection) {
			item = {};
			collection.push(item);
			item[match[1]] = match[2] === '""' ? '' : match[2];
			continue;
		}
		match = /^    ([A-Za-z][A-Za-z0-9]*):(?: (.*))?$/.exec(line);
		if (match && item) {
			item[match[1]] = match[2] === '""' ? '' : match[2];
			continue;
		}
		throw new Error('Unexpected engine log YAML');
	}
	return result;
}

function requestLog(port, command, body = '') {
	return new Promise((resolve, reject) => {
		const socket = net.createConnection({ host: '127.0.0.1', port });
		let reply = '';
		let settled = false;
		function finish(error, value) {
			if (settled) {
				return;
			}
			settled = true;
			socket.destroy();
			if (error) {
				reject(error);
			} else {
				resolve(value);
			}
		}
		socket.setTimeout(2500, () => finish(new Error('Engine log request timed out')));
		socket.on('connect', () => socket.write(`${command}\n${body}${body && !body.endsWith('\n') ? '\n' : ''}...\n`));
		socket.on('data', chunk => {
			reply += chunk.toString('utf8');
			if (Buffer.byteLength(reply, 'utf8') > MAX_REPLY_BYTES) {
				finish(new Error('Engine log reply is too large'));
				return;
			}
			const end = reply.indexOf('\n...\n');
			if (end >= 0) {
				try {
					finish(undefined, parseReply(reply.slice(0, end + 5)));
				} catch (error) {
					finish(error);
				}
			}
		});
		socket.on('error', error => finish(error));
		socket.on('close', () => finish(new Error('Engine log connection closed')));
	});
}

function decode(value) {
	if (!value) {
		return '';
	}
	if (!/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(value)) {
		throw new Error('Invalid encoded engine log field');
	}
	return Buffer.from(value, 'base64').toString('utf8');
}

function formatTime(milliseconds) {
	const date = new Date(Number(milliseconds));
	if (!Number.isFinite(date.getTime())) {
		return '';
	}
	const pad = (number, width = 2) => String(number).padStart(width, '0');
	return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())} `
		+ `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}.${pad(date.getMilliseconds(), 3)}`;
}

function createEngineLogView(api) {
	const changed = new api.EventEmitter();
	const entries = new Map();
	const groups = new Map();
	const groupOccurrences = new Map();
	let session;
	let clear;
	let after = 0n;
	let revision;
	let connected = false;
	let requestGeneration = 0;
	let disposed = false;
	let busy = false;
	let timer;
	const port = () => api.workspace.getConfiguration('jbro.engine').get('controlPort', 3663);
	const t = (message, ...args) => api.l10n.t(message, ...args);

	function updateGroup(group) {
		const id = String(group.id);
		const prior = groups.get(id);
		if (prior && BigInt(prior.latest) > BigInt(group.latest)) {
			return;
		}
		groups.set(id, group);
	}

	function applyPage(page) {
		if (page.Protocol !== '1' || !page.Session || !page.Clear || !page.Revision || !Array.isArray(page.Entries)) {
			throw new Error('Incomplete engine log page');
		}
		const reset = page.Reset === 'true' || session !== page.Session || clear !== page.Clear;
		const shouldRefresh = reset || revision !== page.Revision || !connected;
		if (reset) {
			entries.clear();
			groups.clear();
			groupOccurrences.clear();
			after = 0n;
		}
		session = page.Session;
		clear = page.Clear;
		if (page.Gap === 'true' && after > 0n) {
			entries.clear();
			groupOccurrences.clear();
		}
		for (const raw of page.Entries) {
			if (!raw.Serial || !raw.TimeMs || !raw.Level || !raw.GroupId
				|| raw.Category64 === undefined || raw.Message64 === undefined) {
				throw new Error('Incomplete engine log occurrence');
			}
			const serial = BigInt(raw.Serial);
			const entry = {
				serial: raw.Serial,
				timeMs: raw.TimeMs,
				level: Number(raw.Level),
				category: decode(raw.Category64),
				message: decode(raw.Message64),
				groupId: raw.GroupId
			};
			if (raw.GroupId !== '0') {
				if (!raw.GroupKey64 || !raw.GroupDisplay64 || !raw.GroupCount
					|| !raw.GroupLatest || !raw.GroupTimeMs || !raw.GroupLevel) {
					throw new Error('Incomplete engine log group');
				}
			}
			entries.set(raw.Serial, entry);
			if (raw.GroupId !== '0') {
				if (!groupOccurrences.has(raw.GroupId)) {
					groupOccurrences.set(raw.GroupId, new Set());
				}
				groupOccurrences.get(raw.GroupId).add(raw.Serial);
				updateGroup({ id: raw.GroupId, key: decode(raw.GroupKey64), count: raw.GroupCount,
					latest: raw.GroupLatest, timeMs: raw.GroupTimeMs,
					display: decode(raw.GroupDisplay64).replace(/###log_group$/, ''), level: Number(raw.GroupLevel) });
			}
			if (serial > after) {
				after = serial;
			}
		}
		const first = BigInt(page.FirstRetained || '0');
		for (const serial of entries.keys()) {
			if (BigInt(serial) < first) {
				const groupId = entries.get(serial).groupId;
				groupOccurrences.get(groupId)?.delete(serial);
				entries.delete(serial);
			}
		}
		revision = page.Revision;
		connected = true;
		view.message = '';
		if (shouldRefresh) {
			changed.fire();
		}
		return page.More === 'true' || groups.size < Number(page.GroupCount || '0');
	}

	async function fetchGroups(generation) {
		let afterId = 0n;
		for (let pageNumber = 0; pageNumber < 1000; ++pageNumber) {
			const page = await requestLog(port(), 'log.groups', `AfterId: ${afterId}\n`);
			if (page.Protocol !== '1') {
				throw new Error('Unsupported engine log protocol');
			}
			if (generation !== requestGeneration) {
				return;
			}
			if (page.Session !== session || page.Clear !== clear) {
				return;
			}
			for (const raw of page.Groups || []) {
				if (!raw.Id || !raw.Key64 || !raw.Display64 || !raw.Count
					|| !raw.Latest || !raw.TimeMs || !raw.Level) {
					throw new Error('Incomplete engine log group');
				}
				updateGroup({ id: raw.Id, key: decode(raw.Key64), count: raw.Count,
					latest: raw.Latest, timeMs: raw.TimeMs,
					display: decode(raw.Display64).replace(/###log_group$/, ''), level: Number(raw.Level) });
				afterId = BigInt(raw.Id);
			}
			if (page.More !== 'true') {
				changed.fire();
				return;
			}
			if (!page.Groups?.length) {
				throw new Error('Engine returned an empty group page with more results');
			}
		}
		throw new Error('Too many engine log group pages');
	}

	async function cycle() {
		if (disposed || busy) {
			return;
		}
		busy = true;
		const generation = requestGeneration;
		let again = false;
		try {
			const body = session ? `After: ${after}\nSession: ${session}\nClear: ${clear}\n` : '';
			const page = await requestLog(port(), 'log.since', body);
			if (generation !== requestGeneration) {
				return;
			}
			again = applyPage(page);
			if (groups.size < Number(page.GroupCount || '0')) {
				await fetchGroups(generation);
			}
		} catch (error) {
			if (generation !== requestGeneration) {
				return;
			}
			connected = false;
			view.message = error.code === 'ECONNREFUSED'
				? t('Engine log is disconnected. Waiting for the engine editor.')
				: t('Engine log could not be read: {0}', String(error.message));
			changed.fire();
		} finally {
			busy = false;
			if (!disposed) {
				timer = setTimeout(cycle, again ? 0 : POLL_MS);
			}
		}
	}

	const provider = {
		onDidChangeTreeData: changed.event,
		getChildren(node) {
			if (node?.kind === 'group') {
				const items = [...(groupOccurrences.get(node.id) || [])].map(serial => entries.get(serial)).filter(Boolean)
					.sort((a, b) => BigInt(a.serial) === BigInt(b.serial) ? 0 : BigInt(a.serial) > BigInt(b.serial) ? -1 : 1)
					.map(entry => ({ kind: 'entry', entry }));
				if (BigInt(node.group.count) > BigInt(items.length)) {
					items.push({ kind: 'omitted' });
				}
				return items;
			}
			if (node) {
				return [];
			}
			const rows = [...groups.values()].map(group => ({ kind: 'group', id: group.id, group }));
			for (const entry of entries.values()) {
				if (entry.groupId === '0') {
					rows.push({ kind: 'entry', entry });
				}
			}
			return rows.sort((a, b) => {
				const left = BigInt(a.kind === 'group' ? a.group.latest : a.entry.serial);
				const right = BigInt(b.kind === 'group' ? b.group.latest : b.entry.serial);
				return left === right ? 0 : left > right ? -1 : 1;
			});
		},
		getTreeItem(node) {
			if (node.kind === 'omitted') {
				return new api.TreeItem(t('Earlier occurrences were omitted'));
			}
			if (node.kind === 'group') {
				const group = node.group;
				const latest = entries.get(group.latest);
				const label = group.display || `${formatTime(group.timeMs)} ${latest?.message || group.key} (${group.count})`;
				const item = new api.TreeItem(label, api.TreeItemCollapsibleState.Collapsed);
				item.id = `group:${session}:${clear}:${node.id}`;
				item.description = latest?.category || group.key;
				item.tooltip = group.key;
				item.iconPath = new api.ThemeIcon(group.level === 4 ? 'error' : group.level === 3 ? 'warning' : 'info');
				return item;
			}
			const entry = node.entry;
			const item = new api.TreeItem(`${formatTime(entry.timeMs)} ${entry.message}`);
			item.id = `entry:${session}:${clear}:${entry.serial}`;
			item.description = entry.category;
			item.iconPath = new api.ThemeIcon(entry.level === 4 ? 'error' : entry.level === 3 ? 'warning' : 'info');
			return item;
		}
	};
	const view = api.window.createTreeView('jbro.engineLogs', { treeDataProvider: provider });
	view.message = t('Engine log is disconnected. Waiting for the engine editor.');
	const show = api.commands.registerCommand('jbro.engineLogs.show', () =>
		api.commands.executeCommand('workbench.view.extension.jbro-engine-logs'));
	const clearCommand = api.commands.registerCommand('jbro.engineLogs.clear', async () => {
		const generation = ++requestGeneration;
		try {
			const result = await requestLog(port(), 'log.clear');
			if (generation !== requestGeneration) {
				return;
			}
			if (result.Protocol !== '1') {
				throw new Error('Unsupported engine log protocol');
			}
			entries.clear();
			groups.clear();
			groupOccurrences.clear();
			after = 0n;
			revision = result.Revision;
			session = result.Session;
			clear = result.Clear;
			changed.fire();
		} catch (error) {
			view.message = error.code === 'ECONNREFUSED'
				? t('Engine log is disconnected. Waiting for the engine editor.')
				: t('Engine log could not be read: {0}', String(error.message));
		}
	});
	const configuration = api.workspace.onDidChangeConfiguration(event => {
		if (event.affectsConfiguration('jbro.engine.controlPort')) {
			++requestGeneration;
			session = undefined;
			clear = undefined;
			after = 0n;
			revision = undefined;
			connected = false;
			entries.clear();
			groups.clear();
			groupOccurrences.clear();
			changed.fire();
		}
	});
	void cycle();
	return {
		dispose() {
			disposed = true;
			clearTimeout(timer);
			configuration.dispose();
			clearCommand.dispose();
			show.dispose();
			view.dispose();
			changed.dispose();
		}
	};
}

module.exports = { parseReply, requestLog, createEngineLogView };
