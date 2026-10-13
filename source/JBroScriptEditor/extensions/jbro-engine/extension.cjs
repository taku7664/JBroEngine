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
	void accounts.refresh();
}

module.exports = { activate };
