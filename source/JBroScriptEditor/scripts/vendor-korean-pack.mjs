// Refresh the bundled Korean language pack from a pinned vscode-loc commit.
// This fetches source files from GitHub; the editor never connects to a marketplace.
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const revision = '0f157dee0cdcbf58cdf51a221ee94751c7f35e75';
const url = `https://raw.githubusercontent.com/microsoft/vscode-loc/${revision}`;
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const target = join(root, 'extensions/jbro-korean');
const source = 'i18n/vscode-language-pack-ko';

async function download(path) {
	const response = await fetch(`${url}/${path}`);
	if (!response.ok) throw new Error(`Downloading ${path}: HTTP ${response.status}`);
	return Buffer.from(await response.arrayBuffer());
}

const sourceManifest = JSON.parse((await download(`${source}/package.json`)).toString('utf8'));
const translations = sourceManifest.contributes.localizations[0].translations;
for (let index = 0; index < translations.length; index += 8) {
	await Promise.all(translations.slice(index, index + 8).map(async ({ path }) => {
		const destination = join(target, path);
		mkdirSync(dirname(destination), { recursive: true });
		writeFileSync(destination, await download(`${source}/${path.replace(/^\.\//, '')}`));
	}));
}
writeFileSync(join(target, 'LICENSE.md'), await download('LICENSE.md'));
const manifest = {
	name: 'jbro-korean',
	displayName: 'JBro Script Editor 한국어',
	description: 'JBro Script Editor에 포함되는 한국어 번역',
	version: '1.0.0',
	publisher: 'jbroengine',
	license: 'MIT',
	engines: { vscode: '^1.137.0' },
	contributes: { localizations: [{
		languageId: 'ko', languageName: 'Korean', localizedLanguageName: '한국어', translations
	}] }
};
writeFileSync(join(target, 'package.json'), JSON.stringify(manifest, null, '\t') + '\n');
console.log(`Bundled ${translations.length} translations from microsoft/vscode-loc@${revision}`);
