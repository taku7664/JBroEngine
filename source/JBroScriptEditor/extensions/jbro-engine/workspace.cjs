'use strict';

const path = require('node:path');

// Use the native tree renderer and workspace filesystem; this component does not write files.
function createWorkspaceView(api) {
	const changed = new api.EventEmitter();
	let timer;
	let disposed = false;
	const expanded = new Set();
	const collapsed = new Set();
	const folderIcon = api.Uri.file(path.join(__dirname, 'media', 'icons', 'folder.svg'));
	const folderOpenIcon = api.Uri.file(path.join(__dirname, 'media', 'icons', 'folder-open.svg'));
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
			const key = node.uri.toString();
			const isExpanded = node.root ? !collapsed.has(key) : expanded.has(key);
			const state = node.directory ? (isExpanded ? api.TreeItemCollapsibleState.Expanded : api.TreeItemCollapsibleState.Collapsed) : api.TreeItemCollapsibleState.None;
			const item = new api.TreeItem(node.name, state);
			item.id = node.uri.toString();
			item.resourceUri = node.uri;
			item.iconPath = node.directory
				? (isExpanded ? folderOpenIcon : folderIcon)
				: new api.ThemeIcon('file');
			if (!node.directory) {
				item.command = { command: 'vscode.open', title: '', arguments: [node.uri] };
			}
			return item;
		}
	};
	const view = api.window.createTreeView('jbro.workspace.files', { treeDataProvider: provider });
	const watcher = api.workspace.createFileSystemWatcher('**/*');
	const subscriptions = [view, changed, watcher, api.workspace.onDidChangeWorkspaceFolders(refresh),
		view.onDidExpandElement(({ element }) => { const key = element.uri.toString(); collapsed.delete(key); expanded.add(key); changed.fire(element); }),
		view.onDidCollapseElement(({ element }) => { const key = element.uri.toString(); expanded.delete(key); collapsed.add(key); changed.fire(element); }),
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
