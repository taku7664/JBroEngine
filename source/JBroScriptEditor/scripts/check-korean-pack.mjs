import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const extension = join(root, 'extensions/jbro-korean');
const manifest = JSON.parse(readFileSync(join(extension, 'package.json'), 'utf8'));
const localization = manifest.contributes.localizations[0];
assert.equal(localization.languageId, 'ko');
assert.ok(localization.translations.length >= 90);
for (const { id, path } of localization.translations) {
	assert.ok(existsSync(join(extension, path)), `Missing ${id} translation`);
	JSON.parse(readFileSync(join(extension, path), 'utf8'));
}
assert.ok(existsSync(join(extension, 'LICENSE.md')));

const main = JSON.parse(readFileSync(join(extension, 'translations/main.i18n.json'), 'utf8')).contents;
const expected = [
	['vs/workbench/contrib/workspace/browser/workspace.contribution', 'restrictedModeBannerMessageFolder'],
	['vs/workbench/contrib/workspace/browser/workspace.contribution', 'restrictedModeBannerManage'],
	['vs/workbench/contrib/workspace/browser/workspace.contribution', 'restrictedModeBannerLearnMore'],
	['vs/workbench/contrib/markers/browser/messages', 'markers.panel.no.problems.build'],
	['vs/workbench/contrib/markers/browser/markers.contribution', 'problems'],
	['vs/workbench/contrib/output/browser/output.contribution', 'output'],
	['vs/workbench/contrib/debug/browser/debug.contribution', 'debugPanel'],
	['vs/workbench/contrib/terminal/browser/terminal.contribution', 'terminal']
];
for (const [module, key] of expected) {
	assert.match(main[module]?.[key] ?? '', /[가-힣]/, `Untranslated ${module}:${key}`);
}
console.log(`Korean core and ${localization.translations.length - 1} extension translations are present.`);
