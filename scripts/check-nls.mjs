// Checks that every JBro extension is translated completely.
//
// VS Code falls back to the default (English) file silently when a key is missing, so a
// missing translation cannot be seen on screen. This script makes it a test failure instead.
//
// For every folder under extensions/:
// - every %key% used in package.json exists in package.nls.json
// - package.nls.ko.json exists (the editor opens in Korean by default)
// - every package.nls.<locale>.json has exactly the keys of package.nls.json, none empty
// - the same rule for l10n/bundle.l10n.json and l10n/bundle.l10n.<locale>.json when present

import fs from 'node:fs';
import path from 'node:path';

const root = path.resolve(import.meta.dirname, '..');
const extensionsDir = path.join(root, 'extensions');
const requiredLocales = ['ko'];
const failures = [];

function readJson(file) {
	return JSON.parse(fs.readFileSync(file, 'utf8'));
}

function collectPlaceholders(value, found) {
	if (typeof value === 'string') {
		const match = /^%(.+)%$/.exec(value);
		if (match) {
			found.add(match[1]);
		}
		return;
	}
	if (Array.isArray(value)) {
		for (const item of value) {
			collectPlaceholders(item, found);
		}
		return;
	}
	if (value !== null && typeof value === 'object') {
		for (const item of Object.values(value)) {
			collectPlaceholders(item, found);
		}
	}
}

function compareLocales(label, dir, defaultName, localizedPattern, localizedName) {
	const defaultFile = path.join(dir, defaultName);
	if (!fs.existsSync(defaultFile)) {
		return null;
	}
	const defaults = readJson(defaultFile);
	const defaultKeys = Object.keys(defaults).sort();

	for (const locale of requiredLocales) {
		if (!fs.existsSync(path.join(dir, localizedName(locale)))) {
			failures.push(`${label}: ${localizedName(locale)} is missing`);
		}
	}

	for (const name of fs.readdirSync(dir)) {
		if (!localizedPattern.test(name)) {
			continue;
		}
		const localized = readJson(path.join(dir, name));
		const localizedKeys = Object.keys(localized).sort();
		for (const key of defaultKeys) {
			if (!(key in localized)) {
				failures.push(`${label}: ${name} has no translation for "${key}"`);
			} else if (typeof localized[key] !== 'string' || localized[key].trim() === '') {
				failures.push(`${label}: ${name} has an empty translation for "${key}"`);
			}
		}
		for (const key of localizedKeys) {
			if (!(key in defaults)) {
				failures.push(`${label}: ${name} has "${key}" which ${defaultName} does not have`);
			}
		}
	}
	return defaults;
}

const extensionNames = fs.readdirSync(extensionsDir, { withFileTypes: true })
	.filter(entry => entry.isDirectory())
	.map(entry => entry.name);

if (extensionNames.length === 0) {
	failures.push('no extensions found - the check would pass without checking anything');
}

for (const name of extensionNames) {
	const dir = path.join(extensionsDir, name);
	const manifest = readJson(path.join(dir, 'package.json'));

	const used = new Set();
	collectPlaceholders(manifest, used);

	const defaults = compareLocales(
		name,
		dir,
		'package.nls.json',
		/^package\.nls\.[A-Za-z-]+\.json$/,
		locale => `package.nls.${locale}.json`);

	if (used.size > 0 && defaults === null) {
		failures.push(`${name}: package.json uses %keys% but package.nls.json is missing`);
	}
	for (const key of used) {
		if (defaults !== null && !(key in defaults)) {
			failures.push(`${name}: package.json uses %${key}% which package.nls.json does not define`);
		}
	}

	const l10nDir = path.join(dir, 'l10n');
	if (fs.existsSync(l10nDir)) {
		compareLocales(
			`${name}/l10n`,
			l10nDir,
			'bundle.l10n.json',
			/^bundle\.l10n\.[A-Za-z-]+\.json$/,
			locale => `bundle.l10n.${locale}.json`);
	}

	console.log(`checked ${name}: ${used.size} manifest key(s)`);
}

if (failures.length > 0) {
	for (const failure of failures) {
		console.error(`FAIL ${failure}`);
	}
	process.exit(1);
}
console.log('all translations complete');
