'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vscode = require('vscode');

async function eventually(check, label) {
	const deadline = Date.now() + 30000;
	while (Date.now() < deadline) {
		const value = await check();
		if (value) {
			return value;
		}
		await new Promise(resolve => setTimeout(resolve, 200));
	}
	throw new Error(`Timed out waiting for ${label}; current diagnostics: ${JSON.stringify(
		vscode.languages.getDiagnostics().map(([uri, items]) => [uri.toString(), items.map(item => item.code)]))}`);
}

async function run() {
	const folder = vscode.workspace.workspaceFolders?.[0];
	assert.ok(folder, 'Open the sample project folder for this extension-host test');
	const extension = vscode.extensions.getExtension('jbro.jbro-languages');
	assert.ok(extension, 'JBroScript language extension is loaded');
	await extension.activate();

	const scriptPath = path.join(folder.uri.fsPath, 'Contents', 'Scripts', 'PatrolProbe.jscript');
	const original = fs.readFileSync(scriptPath, 'utf8');
	const document = await vscode.workspace.openTextDocument(vscode.Uri.file(scriptPath));
	const editor = await vscode.window.showTextDocument(document);
	assert.equal(document.languageId, 'jbroscript');
	assert.equal(vscode.workspace.isTrusted, true, 'The test project must be a trusted workspace');

	const completionOffset = original.indexOf('Time.DeltaTime') + 'Time.'.length;
	assert.ok(completionOffset >= 'Time.'.length);
	const completionPosition = document.positionAt(completionOffset);
	await eventually(async () => {
		const result = await vscode.commands.executeCommand('vscode.executeCompletionItemProvider', document.uri, completionPosition);
		return result?.items.some(item => item.label === 'DeltaTime');
	}, 'JBroScript completion from JBroc');

	const guard = '        if (target is null)\n        {\n            return 0.0\n        }\n';
	const start = original.indexOf(guard);
	assert.ok(start >= 0);
	assert.equal(await editor.edit(edit => edit.delete(new vscode.Range(document.positionAt(start), document.positionAt(start + guard.length)))), true);
	await eventually(() => vscode.languages.getDiagnostics(document.uri).some(item => item.code === 'JBC3007'),
		'nullable access diagnostic in Problems');
	assert.equal(fs.readFileSync(scriptPath, 'utf8'), original, 'unsaved edit must not change the file');

	assert.equal(await editor.edit(edit => edit.insert(document.positionAt(start), guard)), true);
	await eventually(() => vscode.languages.getDiagnostics(document.uri).length === 0,
		'diagnostic clearing after the guard is restored');

	const declaration = original.indexOf('script ');
	assert.ok(declaration >= 0);
	assert.equal(await editor.edit(edit => edit.replace(
		new vscode.Range(document.positionAt(declaration), document.positionAt(declaration + 'script'.length)), 'scri!t')), true);
	await eventually(() => vscode.languages.getDiagnostics(document.uri).some(item => item.code === 'JBC2001'),
		'parser error diagnostic in Problems');
	assert.equal(await editor.edit(edit => edit.replace(
		new vscode.Range(document.positionAt(declaration), document.positionAt(declaration + 'scri!t'.length)), 'script')), true);
	await eventually(() => vscode.languages.getDiagnostics(document.uri).length === 0,
		'diagnostic clearing after syntax is restored');
	assert.equal(fs.readFileSync(scriptPath, 'utf8'), original, 'the test must not save the invalid script');
	console.log('JBroScript editor LSP checks passed (completion, syntax and semantic Problems, unsaved edits, clearing).');
}

module.exports = { run };
