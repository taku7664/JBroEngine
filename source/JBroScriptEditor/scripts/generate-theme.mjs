// Engine semantic colors stay in EditorTheme.h. This adapter maps them to the
// existing Code-OSS theme service; reusable native widgets consume its CSS variables.
import fs from 'node:fs';
import path from 'node:path';

const root = path.resolve(import.meta.dirname, '..');
const source = path.resolve(root, '../JBroEngine/Modules/JBroEditor/Include/JBro/Editor/EditorTheme.h');
const themeDir = path.join(root, 'extensions/jbro-engine/themes');
const palette = new Map();
const pattern = /constexpr\s+ImVec4\s+(\w+)\(([^)]+)\);/g;
for (const match of fs.readFileSync(source, 'utf8').matchAll(pattern)) {
	const values = match[2].split(',').map(value => Number(value.trim().replace(/f$/, '')));
	if (values.length === 4 && values.every(value => Number.isFinite(value) && value >= 0 && value <= 1)) {
		const bytes = values.map(value => Math.round(value * 255).toString(16).padStart(2, '0'));
		palette.set(match[1], '#' + bytes.slice(0, values[3] === 1 ? 3 : 4).join('').toUpperCase());
	}
}
function color(role) {
	if (!palette.has(role)) {
		throw new Error(`EditorTheme.h has no supported color definition for ${role}`);
	}
	return palette.get(role);
}
function fade(role, alpha) {
	return color(role).slice(0, 7) + Math.round(alpha * 255).toString(16).padStart(2, '0').toUpperCase();
}
const groups = {
	Workspace: ['editor.background', 'editorGutter.background', 'editorGroup.emptyBackground', 'statusBar.background', 'statusBar.noFolderBackground', 'terminal.background', 'activityBar.background'],
	Panel: ['sideBar.background', 'panel.background', 'sideBarSectionHeader.background', 'editorWidget.background'],
	Raised: ['titleBar.activeBackground', 'titleBar.inactiveBackground', 'menu.background', 'dropdown.background', 'quickInput.background', 'notifications.background'],
	Input: ['input.background', 'button.secondaryBackground'],
	Hover: ['list.hoverBackground', 'toolbar.hoverBackground', 'button.secondaryHoverBackground'],
	TabStripActive: ['editorGroupHeader.tabsBackground', 'editorGroupHeader.noTabsBackground'],
	TabIdle: ['tab.inactiveBackground', 'tab.unfocusedInactiveBackground'],
	TabTop: ['tab.activeBackground', 'tab.unfocusedActiveBackground'],
	Line: ['sideBar.border', 'panel.border', 'menu.border', 'dropdown.border', 'input.border', 'tab.border', 'editorGroup.border', 'editorWidget.border', 'widget.border'],
	LineStrong: ['editorLineNumber.foreground', 'tab.unfocusedActiveBorderTop', 'scrollbarSlider.hoverBackground'],
	Ink: ['foreground', 'editor.foreground', 'sideBar.foreground', 'sideBarTitle.foreground', 'menu.foreground', 'input.foreground', 'tab.activeForeground', 'titleBar.activeForeground', 'statusBar.foreground', 'terminal.foreground', 'panelTitle.activeForeground', 'button.foreground'],
	InkMuted: ['descriptionForeground', 'disabledForeground', 'editorLineNumber.activeForeground', 'tab.inactiveForeground', 'titleBar.inactiveForeground', 'panelTitle.inactiveForeground', 'menu.selectionForeground'],
	Accent: ['focusBorder', 'tab.activeBorderTop', 'panelTitle.activeBorder', 'button.background', 'progressBar.background', 'editorCursor.foreground', 'sash.hoverBorder'],
	AccentBright: ['textLink.foreground', 'textLink.activeForeground', 'button.hoverBackground'],
	Amber: ['editorWarning.foreground', 'list.warningForeground'],
	Danger: ['editorError.foreground', 'errorForeground', 'list.errorForeground'],
	Success: ['gitDecoration.addedResourceForeground']
};
const colors = {};
for (const [role, keys] of Object.entries(groups)) {
	for (const key of keys) {
		colors[key] = color(role);
	}
}
Object.assign(colors, {
	'menu.selectionBackground': fade('Accent', 0.38),
	'menu.selectionForeground': color('Ink'),
	'list.activeSelectionBackground': fade('Accent', 0.38),
	'list.activeSelectionForeground': color('Ink'),
	'list.inactiveSelectionBackground': color('Hover'),
	'list.focusOutline': color('Accent'),
	'editor.selectionBackground': fade('Accent', 0.35),
	'editor.inactiveSelectionBackground': fade('Accent', 0.20),
	'editor.lineHighlightBackground': fade('Raised', 0.50),
	'scrollbarSlider.background': fade('Hover', 0.80),
	'scrollbarSlider.activeBackground': fade('Accent', 0.80),
	'editorIndentGuide.background1': color('Line'),
	'editorIndentGuide.activeBackground1': color('LineStrong')
});
const syntax = JSON.parse(fs.readFileSync(path.join(themeDir, 'syntax-tokens.json'), 'utf8'));
for (const key of ['keyword', 'type', 'function', 'string', 'number', 'punctuation']) {
	if (!/^#[0-9A-Fa-f]{6}$/.test(syntax[key])) {
		throw new Error(`Invalid syntax color: ${key}`);
	}
}
const theme = {
	name: 'JBroEngine Dark', type: 'dark', semanticHighlighting: true, colors,
	tokenColors: [
		{ scope: ['comment'], settings: { foreground: color('InkMuted'), fontStyle: 'italic' } },
		{ scope: ['keyword', 'storage'], settings: { foreground: syntax.keyword } },
		{ scope: ['entity.name.type', 'support.type', 'entity.name.namespace'], settings: { foreground: syntax.type } },
		{ scope: ['entity.name.function', 'support.function'], settings: { foreground: syntax.function } },
		{ scope: ['string'], settings: { foreground: syntax.string } },
		{ scope: ['constant.numeric', 'constant.language'], settings: { foreground: syntax.number } },
		{ scope: ['punctuation', 'keyword.operator'], settings: { foreground: syntax.punctuation } }
	],
	semanticTokenColors: { keyword: syntax.keyword, type: syntax.type, class: syntax.type, interface: syntax.type, namespace: syntax.type, function: syntax.function, method: syntax.function, string: syntax.string, number: syntax.number, comment: color('InkMuted') }
};
const target = path.join(themeDir, 'jbro-engine-color-theme.json');
const contents = JSON.stringify(theme, null, '\t') + '\n';
if (process.argv.includes('--check')) {
	if (!fs.existsSync(target) || fs.readFileSync(target, 'utf8') !== contents) {
		console.error('JBroEngine theme is missing or stale. Run npm run generate:theme.');
		process.exitCode = 1;
	} else {
		console.log('JBroEngine theme matches the engine palette and syntax tokens.');
	}
} else {
	fs.writeFileSync(target, contents);
	console.log(`Generated ${Object.keys(colors).length} semantic colors from EditorTheme.h.`);
}
