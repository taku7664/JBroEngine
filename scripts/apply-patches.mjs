// Applies the core patches in patches/ to the upstream Code-OSS checkout in upstream/, in file-name order.
//
//   node scripts/apply-patches.mjs           apply every patch
//   node scripts/apply-patches.mjs --check   only report whether every patch would apply
//
// The checkout has to be clean first, so that what ends up in upstream/ is exactly upstream plus the
// patches. To go back to a clean checkout: git -C upstream checkout -- .
//
// All patches go to one `git apply` call. It applies them in the order given and applies nothing
// when any of them fails, so a half-patched tree is never left behind.
//
// Why each patch exists is in the engine repo, tasks/ide-plan.md section 5.2.

import { execFileSync } from 'node:child_process';
import { readdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const upstream = join(root, 'upstream');
const patchDir = join(root, 'patches');
const checkOnly = process.argv.includes('--check');

function git(args) {
	return execFileSync('git', ['-C', upstream, ...args], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
}

const patches = readdirSync(patchDir)
	.filter(name => /^\d{4}-.+\.patch$/.test(name))
	.sort()
	.map(name => join(patchDir, name));

if (patches.length === 0) {
	console.error(`no patches found in ${patchDir}`);
	process.exit(1);
}

const dirty = git(['status', '--porcelain', '--untracked-files=no']).trim();
if (dirty) {
	console.error('upstream/ has local changes; run `git -C upstream checkout -- .` first:');
	console.error(dirty);
	process.exit(1);
}

const tag = git(['describe', '--tags', '--always']).trim();
console.log(`upstream ${tag}`);
for (const patch of patches) {
	console.log(`  ${checkOnly ? 'check' : 'apply'} ${patch.slice(patchDir.length + 1)}`);
}

try {
	git(['apply', ...(checkOnly ? ['--check'] : []), ...patches]);
} catch (error) {
	console.error(error.stderr || error.message);
	console.error(checkOnly ? 'the patches do not apply' : 'nothing was applied');
	process.exit(1);
}

console.log(checkOnly ? 'every patch applies' : `applied ${patches.length} patch(es)`);
