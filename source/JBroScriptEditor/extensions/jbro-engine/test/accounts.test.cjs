const assert = require('node:assert/strict');
const { test } = require('node:test');
const { createGitHubAccounts, GITHUB_SCOPES } = require('../accounts.cjs');
const { spawnSync } = require('node:child_process');
const path = require('node:path');

function deferred() {
	let resolve;
	let reject;
	const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
	return { promise, resolve, reject };
}

function fixture(getSession = async () => undefined) {
	const calls = [];
	const errors = [];
	const commands = [];
	let listener;
	const item = { show() {}, dispose() { this.disposed = true; } };
	const api = {
		authentication: {
			getSession: (...args) => { calls.push(args); return getSession(...args); },
			onDidChangeSessions: callback => {
				listener = callback;
				return { dispose() { listener = undefined; } };
			}
		},
		window: { createStatusBarItem: () => item, showErrorMessage: message => { errors.push(message); } },
		commands: { executeCommand: async (...args) => { commands.push(args); } },
		StatusBarAlignment: { Right: 2 },
		l10n: { t: (message, ...args) => message.replace(/\{(\d+)\}/g, (_, index) => String(args[index])) }
	};
	const accounts = createGitHubAccounts(api);
	return { accounts, item, calls, errors, commands, changed: provider => listener?.({ provider: { id: provider } }) };
}

const session = { account: { id: '123', label: 'example-user' }, accessToken: 'secret-test-value', scopes: GITHUB_SCOPES };

test('startup uses a silent request and leaves a visible login entry without a session', async () => {
	const f = fixture();
	await f.accounts.refresh();
	assert.deepEqual(f.calls, [['github', GITHUB_SCOPES, { silent: true }]]);
	assert.match(f.item.text, /Sign in to GitHub/);
	assert.equal(f.item.command, 'jbro.git.signIn');
	f.accounts.dispose();
});

test('sign-in delegates to the existing provider and displays only the account label', async () => {
	const f = fixture(async () => session);
	assert.equal(await f.accounts.signIn(), true);
	assert.deepEqual(f.calls, [['github', GITHUB_SCOPES, { createIfNone: true }]]);
	assert.match(f.item.text, /example-user/);
	assert.equal(f.item.command, 'jbro.git.manageAccount');
	assert.equal(JSON.stringify(f.item).includes(session.accessToken), false);
	assert.equal(JSON.stringify(f.commands).includes(session.accessToken), false);
	await f.accounts.manageAccount();
	assert.deepEqual(f.commands.at(-1), ['workbench.action.manageAccounts']);
	f.accounts.dispose();
});

test('rapid repeated clicks start only one interactive authentication request', async () => {
	const pending = deferred();
	const f = fixture(() => pending.promise);
	const first = f.accounts.signIn();
	const second = f.accounts.signIn();
	assert.equal(f.calls.length, 1);
	assert.match(f.item.text, /Signing in/);
	pending.resolve(session);
	assert.deepEqual(await Promise.all([first, second]), [true, true]);
	f.accounts.dispose();
});

test('cancellation restores the login entry without an error notification', async () => {
	const f = fixture(async () => { throw new Error('User Cancelled'); });
	assert.equal(await f.accounts.signIn(), false);
	assert.deepEqual(f.errors, []);
	assert.match(f.item.text, /Sign in to GitHub/);
	f.accounts.dispose();
});

test('authentication failures do not echo provider error text or secrets', async () => {
	const f = fixture(async () => { throw new Error('request failed with secret-test-value'); });
	assert.equal(await f.accounts.signIn(), false);
	assert.deepEqual(f.errors, ['GitHub sign-in failed. Try again.']);
	assert.equal(JSON.stringify(f.item).includes('secret-test-value'), false);
	f.accounts.dispose();
});

test('latest session refresh wins when provider requests finish out of order', async () => {
	const old = deferred();
	let count = 0;
	const f = fixture(() => ++count === 1 ? old.promise : Promise.resolve(session));
	const first = f.accounts.refresh();
	await f.accounts.refresh();
	old.resolve(undefined);
	await first;
	assert.match(f.item.text, /example-user/);
	f.accounts.dispose();
});

test('GitHub session removal updates the view and other providers are ignored', async () => {
	let current = session;
	const f = fixture(async () => current);
	await f.accounts.refresh();
	await f.changed('microsoft');
	assert.equal(f.calls.length, 1);
	current = undefined;
	await f.changed('github');
	assert.match(f.item.text, /Sign in to GitHub/);
	f.accounts.dispose();
});

test('disposal prevents late authentication results from changing the view', async () => {
	const pending = deferred();
	const f = fixture(() => pending.promise);
	const first = f.accounts.signIn();
	f.accounts.dispose();
	const before = f.item.text;
	pending.resolve(session);
	await first;
	assert.equal(f.item.text, before);
	assert.equal(f.item.disposed, true);
});

test('launcher rejects duplicate scalar profile options before starting Electron', { skip: process.platform !== 'win32' }, () => {
	const launcher = path.resolve(__dirname, '../../../scripts/upstream-launch.cmd');
	for (const argument of ['--user-data-dir=fake-one --user-data-dir=fake-two', '--extensions-dir=fake-one']) {
		const result = spawnSync('cmd.exe', ['/d', '/s', '/c', `""${launcher}" ${argument}"`], { encoding: 'utf8', windowsVerbatimArguments: true });
		assert.equal(result.status, 1);
		assert.match(result.stdout, /upstream-launch:/);
	}
});
