import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
const data=JSON.parse(await readFile(new URL('./js-result.json',import.meta.url),'utf8'));
assert.equal(data.sum,332833500);
console.log('ESM_OK');
