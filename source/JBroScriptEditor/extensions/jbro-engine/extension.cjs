'use strict';

const vscode = require('vscode');
const { createGitHubAccounts } = require('./accounts.cjs');
const { createWorkspaceView } = require('./workspace.cjs');
const { createEngineLogView } = require('./engine-logs.cjs');

function activate(context) {
	const accounts = createGitHubAccounts(vscode);
	context.subscriptions.push(
		createWorkspaceView(vscode),
		createEngineLogView(vscode),
		accounts,
		vscode.commands.registerCommand('jbro.git.signIn', () => accounts.signIn()),
		vscode.commands.registerCommand('jbro.git.manageAccount', () => accounts.manageAccount())
	);
	for (const [command, target] of [
		['jbro.layout.showProject', 'workbench.view.extension.jbro-workspace'],
		['jbro.layout.showGit', 'workbench.view.scm'],
		['jbro.tools.build', 'workbench.action.tasks.build'],
		['jbro.tools.run', 'workbench.action.tasks.runTask'],
		['jbro.layout.showDiagnostics', 'workbench.actions.view.problems'],
		['jbro.project.openFolder', 'workbench.action.files.openFolder'],
		['jbro.project.openFile', 'workbench.action.files.openFile'],
		['jbro.tools.showOutput', 'workbench.action.output.toggleOutput'],
		['jbro.tools.showTerminal', 'workbench.action.terminal.focus']
	]) {
		context.subscriptions.push(vscode.commands.registerCommand(command, () => vscode.commands.executeCommand(target)));
	}
	void accounts.refresh();
	void vscode.commands.executeCommand('jbro.workspace.files.focus');
}

module.exports = { activate };
