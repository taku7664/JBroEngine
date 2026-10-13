'use strict';

const assert = require('node:assert/strict');
const vscode = require('vscode');
const { GITHUB_SCOPES } = require('../accounts.cjs');

async function run() {
	const extension = vscode.extensions.getExtension('jbro.jbro-engine');
	assert.ok(extension, 'JBroEngine extension must be loaded');
	await extension.activate();
	const commands = await vscode.commands.getCommands(true);
	for (const command of ['jbro.git.signIn', 'jbro.git.manageAccount', 'workbench.action.manageAccounts']) {
		assert.ok(commands.includes(command), `Command is registered: ${command}`);
	}
	assert.equal(vscode.workspace.getConfiguration('github-authentication').get('preferDeviceCodeFlow'), true);
	const provider = vscode.extensions.getExtension('vscode.github-authentication');
	assert.ok(provider, 'Built-in GitHub provider must be present');
	await provider.activate();
	// A clean test profile must stay signed out; this must not open authentication UI.
	const session = await vscode.authentication.getSession('github', GITHUB_SCOPES, { silent: true });
	assert.equal(session, undefined, 'A fresh profile has no stored GitHub session');
	console.log('JBro GitHub extension-host checks passed (commands, provider, device-flow default, silent startup).');
}

module.exports = { run };
