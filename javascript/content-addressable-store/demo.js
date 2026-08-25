'use strict';

const { ContentAddressableStore } = require('./store.js');

const store = new ContentAddressableStore();

const readmeHash = store.put('this is a small demo repository\n');
const mainJsHash = store.put('function main() {\n  return 42;\n}\n');
const utilJsHash = store.put('function double(x) {\n  return x * 2;\n}\n');
const duplicateReadmeHash = store.put('this is a small demo repository\n');

console.log('objects stored after 4 puts (1 duplicate):', store.size());
console.log('readme hash === duplicate readme hash:', readmeHash === duplicateReadmeHash);

const srcTreeHash = store.putTree([
  { name: 'main.js', hash: mainJsHash, type: 'blob' },
  { name: 'util.js', hash: utilJsHash, type: 'blob' },
]);

const rootTreeHash = store.putTree([
  { name: 'README.md', hash: readmeHash, type: 'blob' },
  { name: 'src', hash: srcTreeHash, type: 'tree' },
]);

console.log('\nroot tree hash:', rootTreeHash);
console.log('src tree hash:', srcTreeHash);

console.log('\nwalking the whole tree from the root hash:');
console.log(JSON.stringify(store.walkTree(rootTreeHash), null, 2));

console.log('\nresolving a single path (src/util.js):');
console.log(store.resolvePath(rootTreeHash, 'src/util.js'));

console.log('\nresolving a subtree path (src):');
console.log(JSON.stringify(store.resolvePath(rootTreeHash, 'src'), null, 2));

console.log('\ntotal distinct objects stored (blobs + trees):', store.size());

try {
  store.resolvePath(rootTreeHash, 'src/does-not-exist.js');
} catch (err) {
  console.log('\nlooking up a missing path correctly failed:', err.message);
}

const tamperedHash = store.put('will be corrupted');
store._objects.set(tamperedHash, Buffer.from('corrupted bytes that do not match the hash'));
try {
  store.get(tamperedHash);
} catch (err) {
  console.log('\nintegrity check correctly rejected tampered storage:', err.name, '-', err.message);
}
