'use strict';

// Use the native tree renderer and workspace filesystem; this component does not write files.
function createWorkspaceView(api) {
	const changed = new api.EventEmitter();
	let timer;
	let disposed = false;
	function refresh() {
		clearTimeout(timer);
		timer = setTimeout(() => {
			if (!disposed) {
				changed.fire();
			}
		}, 100);
	}
	const provider = {
		onDidChangeTreeData: changed.event,
		async getChildren(node) {
			if (!node) {
				return (api.workspace.workspaceFolders ?? []).map(folder => ({ uri: folder.uri, name: folder.name, directory: true, root: true }));
			}
			const entries = await api.workspace.fs.readDirectory(node.uri);
			return entries.filter(([name]) => name !== '.git')
				.map(([name, type]) => ({ uri: api.Uri.joinPath(node.uri, name), name, directory: Boolean(type & api.FileType.Directory) }))
				.sort((a, b) => Number(b.directory) - Number(a.directory) || a.name.localeCompare(b.name));
		},
		getTreeItem(node) {
			const state = node.directory ? (node.root ? api.TreeItemCollapsibleState.Expanded : api.TreeItemCollapsibleState.Collapsed) : api.TreeItemCollapsibleState.None;
			const item = new api.TreeItem(node.name, state);
			item.id = node.uri.toString();
			item.resourceUri = node.uri;
			item.iconPath = new api.ThemeIcon(node.directory ? 'folder' : 'file');
			if (!node.directory) {
				item.command = { command: 'vscode.open', title: '', arguments: [node.uri] };
			}
			return item;
		}
	};
	const view = api.window.createTreeView('jbro.workspace.files', { treeDataProvider: provider });
	const watcher = api.workspace.createFileSystemWatcher('**/*');
	const subscriptions = [view, changed, watcher, api.workspace.onDidChangeWorkspaceFolders(refresh),
		watcher.onDidCreate(refresh), watcher.onDidDelete(refresh),
		api.commands.registerCommand('jbro.workspace.refresh', refresh)];
	return {
		dispose() {
			disposed = true;
			clearTimeout(timer);
			for (const subscription of subscriptions) {
				subscription.dispose();
			}
		}
	};
}

module.exports = { createWorkspaceView };
