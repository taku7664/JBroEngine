'use strict';

const vscode = require('vscode');
const path = require('node:path');
const { createGitHubAccounts } = require('./accounts.cjs');
const { createWorkspaceView } = require('./workspace.cjs');
const { createEngineLogView } = require('./engine-logs.cjs');

// 디버그 어댑터(JBroDebugAdapter.exe)가 있는 곳을 고른다. 설정이 있으면 그것, 없으면 확장 폴더 옆의 관례 자리, 끝으로 이름만(= PATH).
// 떠 있는 씬 에디터에 세우지 않고 붙으므로, 여기서는 띄울 실행 파일만 고르면 된다(붙을 프로세스와 통로는 어댑터가 찾는다).
function resolveAdapterPath() {
	const configured = vscode.workspace.getConfiguration('jbro.engine').get('debugAdapterPath');
	if (typeof configured === 'string' && configured.length > 0) {
		return configured;
	}
	const name = process.platform === 'win32' ? 'JBroDebugAdapter.exe' : 'JBroDebugAdapter';
	// 설치본에서는 어댑터를 확장 옆 `debug-adapter/` 에 둔다(설치 모양이 서면 그 자리다). 없으면 이름만 넘겨 PATH 에서 찾게 한다.
	return path.join(__dirname, 'debug-adapter', name);
}

function activate(context) {
	const accounts = createGitHubAccounts(vscode);
	context.subscriptions.push(
		createWorkspaceView(vscode),
		createEngineLogView(vscode),
		accounts,
		vscode.commands.registerCommand('jbro.git.signIn', () => accounts.signIn()),
		vscode.commands.registerCommand('jbro.git.manageAccount', () => accounts.manageAccount()),
		// **스크립트 디버깅**(D-330). F5 나 `디버그 실행` 이 씬 에디터에 세우지 않고 붙는다. 어댑터는 VS Code 가 이 서술자로 띄운다.
		vscode.debug.registerDebugAdapterDescriptorFactory('jbro', {
			createDebugAdapterDescriptor() {
				return new vscode.DebugAdapterExecutable(resolveAdapterPath(), []);
			}
		}),
		vscode.commands.registerCommand('jbro.debug.start', () => vscode.debug.startDebugging(undefined, {
			type: 'jbro',
			request: 'attach',
			name: vscode.l10n.t('Attach to the scene editor'),
			processName: 'JBroEditorHost.exe'
		}))
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
