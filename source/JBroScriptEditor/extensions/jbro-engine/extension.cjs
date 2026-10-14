'use strict';

const vscode = require('vscode');
const { createGitHubAccounts } = require('./accounts.cjs');

function activate(context) {
	const accounts = createGitHubAccounts(vscode);
	context.subscriptions.push(
		accounts,
		vscode.commands.registerCommand('jbro.git.signIn', () => accounts.signIn()),
		vscode.commands.registerCommand('jbro.git.manageAccount', () => accounts.manageAccount())
	);
	for (const [command, target] of [
		['jbro.layout.showProject', 'workbench.view.explorer'],
		['jbro.layout.showGit', 'workbench.view.scm'],
		['jbro.tools.build', 'workbench.action.tasks.build'],
		['jbro.tools.run', 'workbench.action.tasks.runTask'],
		['jbro.layout.showDiagnostics', 'workbench.actions.view.problems']
	]) {
		context.subscriptions.push(vscode.commands.registerCommand(command, () => vscode.commands.executeCommand(target)));
	}
	void accounts.refresh();
}

module.exports = { activate };
