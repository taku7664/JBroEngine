'use strict';

const assert = require('node:assert/strict');
const vscode = require('vscode');
const { GITHUB_SCOPES } = require('../accounts.cjs');

async function run() {
	const extension = vscode.extensions.getExtension('jbro.jbro-engine');
	assert.ok(extension, 'JBroEngine extension must be loaded');
	await extension.activate();
	const commands = await vscode.commands.getCommands(true);
	for (const command of ['jbro.git.signIn', 'jbro.git.manageAccount', 'workbench.action.manageAccounts',
		'jbro.layout.showProject', 'jbro.layout.showGit', 'jbro.tools.build', 'jbro.tools.run', 'jbro.layout.showDiagnostics',
		'workbench.view.explorer', 'workbench.view.scm', 'workbench.action.tasks.build', 'workbench.action.tasks.runTask', 'workbench.actions.view.problems']) {
		assert.ok(commands.includes(command), `Command is registered: ${command}`);
	}
	assert.equal(vscode.workspace.getConfiguration('github-authentication').get('preferDeviceCodeFlow'), true);
	assert.equal(vscode.workspace.getConfiguration('workbench.activityBar').get('location'), 'hidden');
	assert.equal(vscode.workspace.getConfiguration('window').get('menuBarVisibility'), 'hidden');
	assert.equal(vscode.workspace.getConfiguration('window').get('commandCenter'), false);
	for (const setting of ['enabled', 'showProject', 'showGit', 'showBuild', 'showRun', 'showDiagnostics']) {
		assert.equal(vscode.workspace.getConfiguration('jbro.layout').get(setting), true, `Layout default: ${setting}`);
	}
	const provider = vscode.extensions.getExtension('vscode.github-authentication');
	assert.ok(provider, 'Built-in GitHub provider must be present');
	await provider.activate();
	// A clean test profile must stay signed out; this must not open authentication UI.
	const session = await vscode.authentication.getSession('github', GITHUB_SCOPES, { silent: true });
	assert.equal(session, undefined, 'A fresh profile has no stored GitHub session');
	console.log('JBro extension-host checks passed (layout defaults, tool commands, GitHub provider, silent startup).');
}

module.exports = { run };
