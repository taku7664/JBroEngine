'use strict';

const fs = require('node:fs');
const path = require('node:path');
const vscode = require('vscode');
const { LanguageClient } = require('vscode-languageclient/node');

const clients = new Map();
const pending = new Map();
const reported = new Map();
let output;
let nextClientId = 1;

function configuredPath(value, folder) {
	return path.isAbsolute(value) ? path.normalize(value) : path.resolve(folder.uri.fsPath, value);
}

async function projectFile(folder) {
	const setting = vscode.workspace.getConfiguration('jbro.jbroscript', folder.uri).get('projectFile', '').trim();
	if (setting) {
		const resolved = configuredPath(setting, folder);
		if (!fs.existsSync(resolved) || !fs.statSync(resolved).isFile()) {
			throw new Error(vscode.l10n.t('The configured .jproject file does not exist: {0}', resolved));
		}
		return resolved;
	}
	const matches = await vscode.workspace.findFiles(new vscode.RelativePattern(folder, '*.jproject'), undefined, 2);
	if (matches.length === 0) {
		throw new Error(vscode.l10n.t('No .jproject file was found at the workspace root. Set jbro.jbroscript.projectFile.'));
	}
	if (matches.length > 1) {
		throw new Error(vscode.l10n.t('More than one .jproject file was found at the workspace root. Set jbro.jbroscript.projectFile.'));
	}
	return matches[0].fsPath;
}

function serverPath(context, folder) {
	const setting = vscode.workspace.getConfiguration('jbro.jbroscript', folder.uri).get('serverPath', '').trim();
	const candidates = setting
		? [configuredPath(setting, folder)]
		: ['Debug', 'Release'].map(configuration => path.resolve(context.extensionPath, '..', '..', '..',
			'JBroEngine', 'Build', 'x64', configuration, 'JBroc.exe'));
	const found = candidates.find(candidate => fs.existsSync(candidate) && fs.statSync(candidate).isFile());
	if (!found) {
		throw new Error(vscode.l10n.t('JBroc.exe was not found. Build the engine or set jbro.jbroscript.serverPath.'));
	}
	return found;
}

function report(folder, error) {
	const message = vscode.l10n.t('JBroScript language server could not start: {0}', String(error?.message ?? error));
	const key = folder?.uri.toString() ?? 'no-folder';
	output.error(message);
	if (reported.get(key) !== message) {
		reported.set(key, message);
		void vscode.window.showWarningMessage(message);
	}
}

async function startForFolder(context, folder) {
	const key = folder.uri.toString();
	if (clients.has(key)) {
		return;
	}
	if (pending.has(key)) {
		return pending.get(key);
	}
	const operation = (async () => {
		let client;
		try {
			const project = await projectFile(folder);
			const executable = serverPath(context, folder);
			client = new LanguageClient(`jbroscript-${nextClientId++}`, 'JBroScript', {
				command: executable,
				args: ['--lsp'],
				options: { cwd: path.dirname(project) }
			}, {
				documentSelector: [{ scheme: 'file', language: 'jbroscript',
					pattern: new vscode.RelativePattern(folder, '**/*.jscript') }],
				workspaceFolder: folder,
				initializationOptions: { projectFile: project },
				outputChannel: output
			});
			await client.start();
			clients.set(key, client);
			reported.delete(key);
			output.info(vscode.l10n.t('JBroScript language server started for {0}', project));
		} catch (error) {
			if (client?.isRunning()) {
				await client.stop().catch(() => {});
			}
			report(folder, error);
		}
	})();
	pending.set(key, operation);
	try {
		await operation;
	} finally {
		pending.delete(key);
	}
}

function startForDocument(context, document) {
	if (document.languageId !== 'jbroscript' || document.uri.scheme !== 'file' || !vscode.workspace.isTrusted) {
		return;
	}
	const folder = vscode.workspace.getWorkspaceFolder(document.uri);
	if (!folder) {
		report(undefined, vscode.l10n.t('Open a JBro project folder to use the JBroScript language server.'));
		return;
	}
	void startForFolder(context, folder);
}

async function stopFolder(key) {
	if (pending.has(key)) {
		await pending.get(key);
	}
	const client = clients.get(key);
	clients.delete(key);
	if (client) {
		await client.stop();
	}
}

async function restart(context) {
	await Promise.all([...new Set([...clients.keys(), ...pending.keys()])].map(key => stopFolder(key)));
	reported.clear();
	for (const document of vscode.workspace.textDocuments) {
		startForDocument(context, document);
	}
}

function activate(context) {
	output = vscode.window.createOutputChannel('JBroScript', { log: true });
	context.subscriptions.push(
		output,
		vscode.workspace.onDidOpenTextDocument(document => startForDocument(context, document)),
		vscode.workspace.onDidChangeConfiguration(event => {
			if (event.affectsConfiguration('jbro.jbroscript.serverPath') ||
				event.affectsConfiguration('jbro.jbroscript.projectFile')) {
				void restart(context).catch(error => report(undefined, error));
			}
		}),
		vscode.workspace.onDidChangeWorkspaceFolders(event => {
			for (const folder of event.removed) {
				void stopFolder(folder.uri.toString()).catch(error => report(folder, error));
			}
		}),
		vscode.workspace.onDidGrantWorkspaceTrust(() => {
			for (const document of vscode.workspace.textDocuments) {
				startForDocument(context, document);
			}
		})
	);
	for (const document of vscode.workspace.textDocuments) {
		startForDocument(context, document);
	}
}

async function deactivate() {
	await Promise.all([...new Set([...clients.keys(), ...pending.keys()])].map(key => stopFolder(key)));
}

module.exports = { activate, deactivate };
