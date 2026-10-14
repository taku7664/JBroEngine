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
		'jbro.project.openFolder', 'jbro.project.openFile', 'jbro.tools.showOutput', 'jbro.tools.showTerminal',
		'jbro.workspace.refresh', 'jbro.workspace.files.focus', 'workbench.view.extension.jbro-workspace',
		'jbro.engineLogs.show', 'jbro.engineLogs.clear', 'jbro.engineLogs.focus', 'workbench.view.extension.jbro-engine-logs',
		'workbench.action.files.openFolder', 'workbench.action.files.openFile', 'workbench.action.output.toggleOutput', 'workbench.action.terminal.focus',
		'workbench.view.explorer', 'workbench.view.scm', 'workbench.action.tasks.build', 'workbench.action.tasks.runTask', 'workbench.actions.view.problems']) {
		assert.ok(commands.includes(command), `Command is registered: ${command}`);
	}
	const manifest = extension.packageJSON;
	assert.equal(manifest.contributes.submenus.length, 5);
	assert.ok(manifest.contributes.menus['jbro/title'].every(item => item.submenu && !item.command), 'Menu headers must open submenus');
	assert.equal(manifest.contributes.views['jbro-workspace'].length, 1);
	assert.equal(manifest.contributes.views['jbro-engine-logs'][0].id, 'jbro.engineLogs');
	assert.equal(vscode.workspace.getConfiguration('jbro.engine').get('controlPort'), 3663);
	assert.equal(vscode.workspace.getConfiguration('workbench').get('colorTheme'), 'jbro-engine-dark');
	assert.equal(vscode.workspace.getConfiguration('github-authentication').get('preferDeviceCodeFlow'), true);
	assert.equal(vscode.workspace.getConfiguration('workbench.activityBar').get('location'), 'hidden');
	assert.equal(vscode.workspace.getConfiguration('workbench.secondarySideBar').get('defaultVisibility'), 'hidden');
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
	console.log('JBro extension-host checks passed (localized menu contributions, tool commands, layout defaults, GitHub provider, silent startup).');
}

module.exports = { run };
