// Register the bundled language pack in this launcher's isolated user-data profile.
// Code-OSS reads languagepacks.json before the extension host starts.
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const userData = process.argv[2];
if (!userData) throw new Error('Pass the launcher user-data directory.');
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const extension = join(root, 'extensions/jbro-korean');
const manifest = JSON.parse(readFileSync(join(extension, 'package.json'), 'utf8'));
const translations = Object.fromEntries(manifest.contributes.localizations[0].translations.map(({ id, path }) => [id, join(extension, path)]));
const cacheFile = join(resolve(userData), 'languagepacks.json');
let cache = {};
if (existsSync(cacheFile)) {
	try { cache = JSON.parse(readFileSync(cacheFile, 'utf8')); }
	catch { throw new Error(`Invalid language pack cache: ${cacheFile}`); }
	if (!cache || typeof cache !== 'object' || Array.isArray(cache)) {
		throw new Error(`Invalid language pack cache: ${cacheFile}`);
	}
}
const id = `${manifest.publisher}.${manifest.name}`;
const previous = cache.ko && typeof cache.ko === 'object' && !Array.isArray(cache.ko) ? cache.ko : {};
const previousTranslations = previous.translations && typeof previous.translations === 'object' && !Array.isArray(previous.translations)
	? previous.translations : {};
const extensions = Array.isArray(previous.extensions)
	? previous.extensions.filter(entry => entry?.extensionIdentifier?.id !== id)
	: [];
extensions.push({ extensionIdentifier: { id }, version: manifest.version });
const hash = createHash('md5');
for (const entry of extensions) {
	hash.update(entry.extensionIdentifier.uuid || entry.extensionIdentifier.id).update(entry.version);
}
hash.update(readFileSync(translations.vscode));
cache.ko = {
	hash: hash.digest('hex'),
	label: '한국어',
	extensions,
	translations: { ...previousTranslations, ...translations }
};
mkdirSync(dirname(cacheFile), { recursive: true });
writeFileSync(cacheFile, JSON.stringify(cache));
console.log(`Registered bundled Korean translations in ${cacheFile}`);
